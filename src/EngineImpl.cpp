#include "EngineImpl.hpp"

#include "threadmaxx/Engine.hpp"
#include "threadmaxx/Game.hpp"
#include "threadmaxx/Renderer.hpp"

#include <algorithm>
#include <latch>
#include <thread>
#include <utility>

namespace threadmaxx::internal {

namespace {

// Picks a sensible chunk size when the caller passes grain=0. Aims for
// roughly 4 chunks per worker so load balances without overwhelming the
// queue with tiny jobs.
std::uint32_t pickGrain(std::uint32_t count, std::uint32_t workers) {
    if (count == 0) return 1;
    const std::uint32_t target = std::max(1u, workers * 4);
    const std::uint32_t grain  = (count + target - 1) / target;
    return std::max(1u, grain);
}

} // namespace

// ---- SystemContextImpl --------------------------------------------------

void SystemContextImpl::parallelFor(std::uint32_t count,
                                    std::uint32_t grain,
                                    JobFn fn) {
    if (count == 0 || !fn) return;

    const std::uint32_t workers = engine_.jobs().workerCount();
    if (grain == 0) grain = pickGrain(count, workers);

    const std::uint32_t chunkCount = (count + grain - 1) / grain;

    // Reserve command buffers up front so emplace_back does not invalidate
    // pointers while jobs are running.
    const std::size_t firstIdx = buffers_.size();
    buffers_.resize(firstIdx + chunkCount);

    std::latch done(chunkCount);
    for (std::uint32_t c = 0; c < chunkCount; ++c) {
        const std::uint32_t begin = c * grain;
        const std::uint32_t end   = std::min(begin + grain, count);
        CommandBuffer* cb = &buffers_[firstIdx + c];
        auto userFn = fn;
        engine_.jobs().submit([userFn, begin, end, cb, &done] {
            userFn(Range{begin, end}, *cb);
            done.count_down();
        });
    }
    jobsSubmitted_ += chunkCount;
    done.wait();
}

void SystemContextImpl::single(JobFn fn) {
    if (!fn) return;
    buffers_.emplace_back();
    fn(Range{0, 0}, buffers_.back());
}

// ---- EngineImpl ---------------------------------------------------------

EngineImpl::EngineImpl(const Config& cfg) : cfg_(cfg) {
    jobs_ = std::make_unique<JobSystem>(cfg_.workerCount);
}

EngineImpl::~EngineImpl() {
    shutdown();
}

bool EngineImpl::initialize(IGame& game, Engine& publicEngine) {
    if (initialized_) return true;
    game_ = &game;
    publicEngine_ = &publicEngine;

    // Recreate the world with the configured capacity. The default-constructed
    // World used a hard-coded 1024; replace it so the user's config wins.
    world_ = World{};
    if (cfg_.initialEntityCapacity > 0) {
        world_.impl_().storage.reserve(cfg_.initialEntityCapacity);
    }

    // Let the game register systems / renderer and seed entities.
    CommandBuffer seed;
    game.onSetup(publicEngine, world_, seed);
    commitBuffer(seed);

    if (renderer_ && !renderer_->initialize()) {
        return false;
    }

    // Build an initial render frame so the renderer can display state at
    // t=0 before any tick has run.
    buildRenderFrame();
    if (renderer_) {
        const unsigned front = frontIndex_.load(std::memory_order_acquire);
        renderer_->submitFrame(renderFrames_[front]);
    }

    initialized_ = true;
    lastIterationTime_ = std::chrono::steady_clock::now();
    return true;
}

void EngineImpl::registerSystem(std::unique_ptr<ISystem> system) {
    if (!system) return;
    system->onRegister(world_);
    systems_.push_back(std::move(system));
    rebuildWaves();
}

void EngineImpl::rebuildWaves() {
    waves_.clear();
    for (std::size_t i = 0; i < systems_.size(); ++i) {
        const auto rI = systems_[i]->reads();
        const auto wI = systems_[i]->writes();

        std::size_t target = waves_.size();  // default: open a new wave
        for (std::size_t w = 0; w < waves_.size(); ++w) {
            bool conflicts = false;
            for (std::size_t j : waves_[w]) {
                const auto rJ = systems_[j]->reads();
                const auto wJ = systems_[j]->writes();
                if (wI.intersects(wJ) || wI.intersects(rJ) || rI.intersects(wJ)) {
                    conflicts = true;
                    break;
                }
            }
            if (!conflicts) { target = w; break; }
        }
        if (target == waves_.size()) waves_.emplace_back();
        waves_[target].push_back(i);
    }
}

void EngineImpl::commitBuffer(CommandBuffer& cb) {
    auto& storage = world_.impl_().storage;
    commandsThisStep_ += cb.commands().size();
    for (auto& cmd : cb.commands()) {
        std::visit([&](auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, detail::CmdSpawn>) {
                const auto h = storage.spawn(c.transform, c.velocity,
                                             c.render, c.userData,
                                             c.acceleration);
                if (c.outHandle) *c.outHandle = h;
            } else if constexpr (std::is_same_v<T, detail::CmdDestroy>) {
                storage.destroy(c.entity);
            } else if constexpr (std::is_same_v<T, detail::CmdSetTransform>) {
                if (auto* p = storage.mutTransform(c.entity)) *p = c.value;
            } else if constexpr (std::is_same_v<T, detail::CmdSetVelocity>) {
                if (auto* p = storage.mutVelocity(c.entity))  *p = c.value;
            } else if constexpr (std::is_same_v<T, detail::CmdSetRenderTag>) {
                if (auto* p = storage.mutRenderTag(c.entity)) *p = c.value;
            } else if constexpr (std::is_same_v<T, detail::CmdSetUserData>) {
                if (auto* p = storage.mutUserData(c.entity))  *p = c.value;
            } else if constexpr (std::is_same_v<T, detail::CmdSetAcceleration>) {
                if (auto* p = storage.mutAcceleration(c.entity)) *p = c.value;
            }
        }, cmd);
    }
    cb.clear();
}

void EngineImpl::buildRenderFrame() {
    const unsigned back = 1u - frontIndex_.load(std::memory_order_acquire);
    auto& dst = renderInstanceBuffers_[back];
    dst.clear();

    const auto& storage = world_.impl_().storage;
    const auto& entities = storage.entities();
    const auto& transforms = storage.transforms();
    const auto& tags = storage.renderTags();
    const auto& uds = storage.userData();
    dst.reserve(entities.size());

    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (tags[i].meshId < 0) continue;  // not renderable
        dst.push_back(RenderInstance{
            entities[i],
            transforms[i],
            tags[i].meshId,
            tags[i].materialId,
            tags[i].flags,
            uds[i].value,
        });
    }

    auto& frame = renderFrames_[back];
    frame.tick = tick_;
    frame.simulationTime = simulationTime_;
    frame.deltaTime = cfg_.fixedStepSeconds;
    frame.alpha = 0.0f;
    frame.instances = std::span<const RenderInstance>(dst.data(), dst.size());

    frontIndex_.store(back, std::memory_order_release);
}

void EngineImpl::submitInterpolatedFrame(float alpha) {
    if (!renderer_) return;
    const unsigned front = frontIndex_.load(std::memory_order_acquire);
    // Mutating alpha on the front frame is safe: submitFrame runs
    // synchronously on the sim thread and the renderer must not retain
    // pointers past it.
    renderFrames_[front].alpha = alpha;
    renderer_->submitFrame(renderFrames_[front]);
}

void EngineImpl::step() {
    if (!initialized_) return;

    const double dt = cfg_.fixedStepSeconds;
    const auto stepStart = std::chrono::steady_clock::now();

    commandsThisStep_ = 0;
    std::uint64_t jobsThisStep = 0;

    // Run systems wave by wave. Within a wave, all systems have pairwise
    // non-conflicting declared read/write sets, so they're safe to drive
    // concurrently — each writes into its own SystemContext's buffer list
    // and reads through `const World&`. Across waves we serialize: a later
    // wave's systems see the previous wave's commits.
    for (const auto& wave : waves_) {
        std::vector<std::unique_ptr<SystemContextImpl>> ctxs;
        ctxs.reserve(wave.size());
        for (std::size_t k = 0; k < wave.size(); ++k) {
            ctxs.push_back(std::make_unique<SystemContextImpl>(
                *this, world_, dt, tick_));
        }

        if (wave.size() == 1) {
            systems_[wave[0]]->update(*ctxs[0]);
        } else {
            // Spawn helper threads for all but the last system in the wave;
            // run the tail on this thread to avoid a wasted join.
            std::vector<std::thread> helpers;
            helpers.reserve(wave.size() - 1);
            for (std::size_t k = 0; k + 1 < wave.size(); ++k) {
                helpers.emplace_back([this, idx = wave[k], ctx = ctxs[k].get()] {
                    systems_[idx]->update(*ctx);
                });
            }
            systems_[wave.back()]->update(*ctxs.back());
            for (auto& t : helpers) t.join();
        }

        // Commit buffers in registration order (wave[] is already in
        // registration order). Sibling systems wrote to disjoint component
        // categories, so commit order among them is observationally a no-op,
        // but we keep it deterministic to make stats and side-effects stable.
        for (std::size_t k = 0; k < wave.size(); ++k) {
            for (auto& cb : ctxs[k]->buffers()) {
                commitBuffer(cb);
            }
            jobsThisStep += ctxs[k]->jobsSubmitted();
        }
    }

    tick_++;
    simulationTime_ += dt;

    buildRenderFrame();
    if (renderer_) {
        const unsigned front = frontIndex_.load(std::memory_order_acquire);
        renderer_->submitFrame(renderFrames_[front]);
    }

    const auto stepEnd = std::chrono::steady_clock::now();
    const double stepSeconds = std::chrono::duration<double>(stepEnd - stepStart).count();

    stats_.tick = tick_;
    stats_.lastStepSeconds = stepSeconds;
    // EWMA with alpha = 1/16. First sample initializes the average.
    if (stats_.totalTicks == 0) {
        stats_.avgStepSeconds = stepSeconds;
    } else {
        constexpr double kEwmaAlpha = 1.0 / 16.0;
        stats_.avgStepSeconds = stats_.avgStepSeconds * (1.0 - kEwmaAlpha)
                              + stepSeconds * kEwmaAlpha;
    }
    stats_.jobsSubmittedLastStep = jobsThisStep;
    stats_.commandsCommittedLastStep = commandsThisStep_;
    stats_.aliveEntities = world_.size();
    stats_.totalTicks++;
    stats_.totalJobsSubmitted += jobsThisStep;
    stats_.totalCommandsCommitted += commandsThisStep_;
}

void EngineImpl::run() {
    if (!initialized_) return;

    lastIterationTime_ = std::chrono::steady_clock::now();
    accumulatedTime_ = 0.0;

    while (!quit_.load(std::memory_order_acquire)) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - lastIterationTime_).count();
        lastIterationTime_ = now;
        accumulatedTime_ += elapsed;

        std::uint32_t stepsThisIter = 0;
        while (accumulatedTime_ >= cfg_.fixedStepSeconds &&
               stepsThisIter < cfg_.maxStepsPerIteration &&
               !quit_.load(std::memory_order_acquire)) {
            step();
            accumulatedTime_ -= cfg_.fixedStepSeconds;
            stepsThisIter++;
        }

        // If we're falling badly behind, drop accumulated time. Avoids the
        // spiral of death where each iteration is owed more time than the
        // last.
        if (accumulatedTime_ > cfg_.fixedStepSeconds * cfg_.maxStepsPerIteration) {
            accumulatedTime_ = 0.0;
        }

        // Hand the renderer an interpolation frame describing where we are
        // between the last committed tick and the next one. step() already
        // submitted alpha=0 immediately after the sim work; this extra
        // submit reflects wall-clock drift since.
        if (renderer_ && stepsThisIter > 0) {
            const float alpha = static_cast<float>(
                accumulatedTime_ / cfg_.fixedStepSeconds);
            submitInterpolatedFrame(alpha);
        }

        if (cfg_.sleepToPace && !quit_.load(std::memory_order_acquire)) {
            const double remaining = cfg_.fixedStepSeconds - accumulatedTime_;
            if (remaining > 0.0) {
                std::this_thread::sleep_for(std::chrono::duration<double>(remaining));
            }
        }
    }
}

void EngineImpl::shutdown() {
    if (!initialized_) return;
    initialized_ = false;

    // Drain any in-flight jobs before we start tearing things down.
    if (jobs_) jobs_->waitIdle();

    if (game_ && publicEngine_) {
        game_->onTeardown(*publicEngine_, world_);
    }

    if (renderer_) {
        renderer_->shutdown();
    }

    for (auto it = systems_.rbegin(); it != systems_.rend(); ++it) {
        (*it)->onUnregister(world_);
    }
    systems_.clear();
}

} // namespace threadmaxx::internal
