/// @file EngineImpl.cpp
/// Heart of the engine. Owns the lifecycle (`initialize` / `step` / `run` /
/// `shutdown`), the wave scheduler, the commit phase, and the double-
/// buffered render-frame publication.
///
/// Maintainer reading order:
///   - `step()` is the canonical tick: reset per-system stats, run waves
///      (each wave fans out across helper threads, with the tail running
///      on the sim thread to avoid a wasted join), commit each system's
///      buffers in registration order, advance tick, build + publish
///      render frame.
///   - `commitBuffer()` is the only path that mutates `EntityStorage`.
///      Every new built-in component or command variant must extend the
///      `std::visit` lambda here.
///   - `rebuildWaves()` is the greedy first-fit packer; it is recomputed
///      every `registerSystem` so wave shape stays consistent with the
///      currently-registered set.
///   - `buildRenderFrame()` is the only path that fills
///      `renderInstanceBuffers_`; publish is via
///      `frontIndex_.store(back, release)`.
#include "EngineImpl.hpp"

#include "threadmaxx/Engine.hpp"
#include "threadmaxx/EventChannel.hpp"
#include "threadmaxx/Game.hpp"
#include "threadmaxx/Renderer.hpp"
#include "threadmaxx/SkipPolicy.hpp"

#include <cstring>

#include <algorithm>
#include <latch>
#include <sstream>
#include <thread>
#include <utility>

namespace threadmaxx::internal {

namespace {

// Picks a sensible chunk size when the caller passes grain=0. The
// active system's `preferredGrain()` (batch 11) wins when it's
// non-zero; otherwise aim for roughly 4 chunks per worker so load
// balances without overwhelming the queue with tiny jobs.
std::uint32_t pickGrain(std::uint32_t count, std::uint32_t workers,
                        std::uint32_t preferred) {
    if (count == 0) return 1;
    if (preferred > 0) {
        return std::min(preferred, std::max(1u, count));
    }
    const std::uint32_t target = std::max(1u, workers * 4);
    const std::uint32_t grain  = (count + target - 1) / target;
    return std::max(1u, grain);
}

} // namespace

// ---- SystemContextImpl --------------------------------------------------

void SystemContextImpl::parallelFor(std::uint32_t count,
                                    std::uint32_t grain,
                                    JobFn fn) {
    parallelFor(count, grain, std::move(fn), JobPriority::Normal);
}

void SystemContextImpl::parallelFor(std::uint32_t count,
                                    std::uint32_t grain,
                                    JobFnArena fn) {
    parallelFor(count, grain, std::move(fn), JobPriority::Normal);
}

void SystemContextImpl::parallelFor(std::uint32_t count,
                                    std::uint32_t grain,
                                    JobFn fn,
                                    JobPriority priority) {
    if (count == 0 || !fn) return;

    const std::uint32_t workers = engine_.jobs().workerCount();
    if (grain == 0) grain = pickGrain(count, workers, preferredGrain_);

    const std::uint32_t chunkCount = (count + grain - 1) / grain;

    // Reserve command buffers up front so emplace_back does not invalidate
    // pointers while jobs are running. arenas_ grows in lockstep; the
    // legacy JobFn variant leaves each entry default-constructed (no
    // allocation paid).
    const std::size_t firstIdx = buffers_.size();
    buffers_.resize(firstIdx + chunkCount);
    arenas_.resize(firstIdx + chunkCount);

    std::latch done(chunkCount);
    for (std::uint32_t c = 0; c < chunkCount; ++c) {
        const std::uint32_t begin = c * grain;
        const std::uint32_t end   = std::min(begin + grain, count);
        CommandBuffer* cb = &buffers_[firstIdx + c];
        auto userFn = fn;
        engine_.jobs().submit([userFn, begin, end, cb, &done] {
            userFn(Range{begin, end}, *cb);
            done.count_down();
        }, priority);
    }
    jobsSubmitted_ += chunkCount;
    // Sample queue depth right after submit — captures congestion the
    // system actually saw. Reading the atomic is cheap.
    const std::uint32_t depth = engine_.jobs().outstanding();
    if (depth > peakQueueDepth_) peakQueueDepth_ = depth;
    const auto waitStart = std::chrono::steady_clock::now();
    done.wait();
    waitSeconds_ += std::chrono::duration<double>(
        std::chrono::steady_clock::now() - waitStart).count();
}

void SystemContextImpl::parallelFor(std::uint32_t count,
                                    std::uint32_t grain,
                                    JobFnArena fn,
                                    JobPriority priority) {
    if (count == 0 || !fn) return;

    const std::uint32_t workers = engine_.jobs().workerCount();
    if (grain == 0) grain = pickGrain(count, workers, preferredGrain_);

    const std::uint32_t chunkCount = (count + grain - 1) / grain;

    const std::size_t firstIdx = buffers_.size();
    buffers_.resize(firstIdx + chunkCount);
    arenas_.resize(firstIdx + chunkCount);

    std::latch done(chunkCount);
    for (std::uint32_t c = 0; c < chunkCount; ++c) {
        const std::uint32_t begin = c * grain;
        const std::uint32_t end   = std::min(begin + grain, count);
        CommandBuffer*  cb    = &buffers_[firstIdx + c];
        ScratchArena*   arena = &arenas_[firstIdx + c];
        auto userFn = fn;
        engine_.jobs().submit([userFn, begin, end, cb, arena, &done] {
            userFn(Range{begin, end}, *cb, *arena);
            done.count_down();
        }, priority);
    }
    jobsSubmitted_ += chunkCount;
    const std::uint32_t depth = engine_.jobs().outstanding();
    if (depth > peakQueueDepth_) peakQueueDepth_ = depth;
    const auto waitStart = std::chrono::steady_clock::now();
    done.wait();
    waitSeconds_ += std::chrono::duration<double>(
        std::chrono::steady_clock::now() - waitStart).count();
}

void SystemContextImpl::single(JobFn fn) {
    if (!fn) return;
    buffers_.emplace_back();
    arenas_.emplace_back();
    fn(Range{0, 0}, buffers_.back());
}

void SystemContextImpl::single(JobFnArena fn) {
    if (!fn) return;
    buffers_.emplace_back();
    arenas_.emplace_back();
    fn(Range{0, 0}, buffers_.back(), arenas_.back());
}

EntityHandle SystemContextImpl::reserveHandle() {
    return engine_.world().impl_().storage.reserveHandle();
}

std::uint32_t SystemContextImpl::reserveHandles(std::uint32_t count,
                                                std::span<EntityHandle> out) {
    const std::uint32_t n = std::min(count,
        static_cast<std::uint32_t>(out.size()));
    engine_.world().impl_().storage.reserveHandles(n, out);
    return n;
}

bool SystemContextImpl::shouldYield() const noexcept {
    return engine_.overBudget();
}

// ---- EngineImpl ---------------------------------------------------------

EngineImpl::EngineImpl(const Config& cfg) : cfg_(cfg) {
    jobs_ = std::make_unique<JobSystem>(cfg_.workerCount);
}

EngineImpl::~EngineImpl() {
    shutdown();
    // §3.3 channels outlive shutdown so postStep hooks can still pump
    // events. They're owned in raw form (factory/deleter pair); release
    // here, after the worker pool has been torn down.
    for (auto& [type, entry] : eventChannels_) {
        if (entry.deleter && entry.ptr) entry.deleter(entry.ptr);
    }
}

bool EngineImpl::initialize(IGame& game, Engine& publicEngine) {
    if (initialized_) return true;
    game_ = &game;
    publicEngine_ = &publicEngine;

    {
        std::ostringstream os;
        os << "engine initialize: " << jobs_->workerCount()
           << " worker(s), fixedStep=" << cfg_.fixedStepSeconds << "s";
        logger().log(LogLevel::Info, os.str());
    }

    // Recreate the world with the configured capacity. The default-constructed
    // World used a hard-coded 1024; replace it so the user's config wins.
    world_ = World{};
    if (cfg_.initialEntityCapacity > 0) {
        world_.impl_().storage.reserve(cfg_.initialEntityCapacity);
    }
    // §3.1 batch 6b: hand the archetype table a pointer to the
    // engine-owned user-component registry. New chunks consult it when
    // materializing per-bit user columns.
    world_.impl_().storage.archetypes().setUserComponentRegistry(&userRegistry_);

    // Let the game register systems / renderer and seed entities.
    CommandBuffer seed;
    game.onSetup(publicEngine, world_, seed);
    commitBuffer(seed);

    if (renderer_ && !renderer_->initialize()) {
        logger().log(LogLevel::Error, "renderer initialize() returned false");
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
    SystemStats ss;
    ss.name = system->name();
    systemStats_.push_back(ss);
    const char* sysName = system->name();
    systemPreferredGrain_.push_back(system->preferredGrain());
    systems_.push_back(std::move(system));
    systemRenderBuilders_.emplace_back();
    rebuildWaves();
    std::ostringstream os;
    os << "registered system '" << (sysName ? sysName : "(unnamed)")
       << "' (now " << systems_.size() << " total, "
       << waves_.size() << " wave(s))";
    logger().log(LogLevel::Info, os.str());
}

IResourceLoader* EngineImpl::addResourceLoader(
        std::unique_ptr<IResourceLoader> loader) {
    if (!loader) return nullptr;
    IResourceLoader* raw = loader.get();
    resourceLoaders_.push_back(std::move(loader));
    return raw;
}

LoaderStats EngineImpl::aggregateLoaderStats() const noexcept {
    LoaderStats agg;
    for (const auto& loader : resourceLoaders_) {
        if (!loader) continue;
        const LoaderStats s = loader->stats();
        agg.pendingLoads    += s.pendingLoads;
        agg.inFlight        += s.inFlight;
        agg.ready           += s.ready;
        agg.failed          += s.failed;
        agg.cancelled       += s.cancelled;
        agg.memoryFootprint += s.memoryFootprint;
        agg.memoryBudget    += s.memoryBudget;
    }
    return agg;
}

void EngineImpl::markResourceStale(std::uint32_t index,
                                   std::uint32_t generation,
                                   std::type_index type) {
    // Loaders are filtered by their own implementation — the one that
    // recognizes the type handles the call, the rest no-op via the
    // default virtual.
    for (auto& loader : resourceLoaders_) {
        if (loader) loader->markStale(index, generation, type);
    }
}

bool EngineImpl::preloadUntil(std::function<bool()> done,
                              std::chrono::milliseconds timeout) {
    if (!done) return false;
    if (!publicEngine_) return false;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        if (done()) return true;
        for (auto& loader : resourceLoaders_) {
            if (loader) loader->update(*publicEngine_);
        }
        // Check again after pumping so a loader that finishes in this
        // pass exits the loop immediately.
        if (done()) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::yield();
    }
}

std::size_t EngineImpl::registerSystemAt(std::size_t position,
                                         std::unique_ptr<ISystem> system) {
    if (!system) return systems_.size();
    if (position > systems_.size()) position = systems_.size();
    system->onRegister(world_);
    SystemStats ss;
    ss.name = system->name();
    systemStats_.insert(systemStats_.begin() +
                        static_cast<std::ptrdiff_t>(position), ss);
    systemPreferredGrain_.insert(systemPreferredGrain_.begin() +
                                 static_cast<std::ptrdiff_t>(position),
                                 system->preferredGrain());
    systems_.insert(systems_.begin() +
                    static_cast<std::ptrdiff_t>(position),
                    std::move(system));
    systemRenderBuilders_.emplace(systemRenderBuilders_.begin() +
                                  static_cast<std::ptrdiff_t>(position));
    rebuildWaves();
    return position;
}

void EngineImpl::rebuildWaves() {
    // §3.4 batch 11: DAG-aware first-fit packer.
    //
    // Edge sources:
    //   1. Read/write conflict (existing behavior): two systems with
    //      overlapping read/write sets must land in different waves.
    //      Modelled as a directed edge from the lower-indexed system to
    //      the higher-indexed one, so the topological tiebreaker matches
    //      registration order in the no-tag case.
    //   2. Tag dependency (new): for each tag a system @c j provides
    //      that some other system @c i lists in `dependencies()`, an
    //      edge `j -> i` (j runs before i). Both directions are
    //      considered — a later-registered system can be the provider
    //      and an earlier-registered one the consumer; the topo sort
    //      moves the consumer to a later wave when needed.
    //
    // Cycle handling: Kahn's algorithm processes systems in index order
    // when in-degrees tie. If a cycle blocks progress, the first stuck
    // system has its tag-only incoming edges dropped (read/write edges
    // are preserved); a warning is logged via @ref ILogger.
    waves_.clear();
    const std::size_t N = systems_.size();
    systemWave_.assign(N, 0);
    systemDependsOn_.assign(N, {});
    if (N == 0) return;

    auto rwConflict = [this](std::size_t a, std::size_t b) -> bool {
        const auto rA = systems_[a]->reads();
        const auto wA = systems_[a]->writes();
        const auto rB = systems_[b]->reads();
        const auto wB = systems_[b]->writes();
        return wA.intersects(wB) || wA.intersects(rB) || rA.intersects(wB);
    };

    auto tagEdge = [this](std::size_t from, std::size_t to) -> bool {
        const auto provides = systems_[from]->provides();
        const auto deps     = systems_[to]->dependencies();
        for (const auto& p : provides) {
            if (!p.valid()) continue;
            for (const auto& d : deps) {
                if (p == d) return true;
            }
        }
        return false;
    };

    std::vector<std::vector<std::size_t>> predecessors(N);
    std::vector<std::vector<std::size_t>> successors(N);
    std::vector<bool> edgeIsTagOnly(0);
    auto addEdge = [&](std::size_t from, std::size_t to, bool tagOnly) {
        predecessors[to].push_back(from);
        successors[from].push_back(to);
        // Track whether the edge is tag-only (so the cycle breaker can
        // distinguish droppable from non-droppable edges).
        edgeIsTagOnly.push_back(tagOnly);
    };
    // Build edges. The (a, b) pair with a<b is iterated once; rw-conflict
    // produces a->b, tag-deps in either direction are checked.
    for (std::size_t a = 0; a < N; ++a) {
        for (std::size_t b = 0; b < N; ++b) {
            if (a == b) continue;
            const bool rw = (a < b) && rwConflict(a, b);
            const bool tag = tagEdge(a, b);
            if (rw || tag) {
                // If both rw and tag, the edge is NOT tag-only.
                addEdge(a, b, /*tagOnly=*/!rw && tag);
            }
        }
    }

    std::vector<std::size_t> inDegree(N, 0);
    for (std::size_t i = 0; i < N; ++i) inDegree[i] = predecessors[i].size();

    std::vector<std::size_t> processed;
    processed.reserve(N);
    std::vector<bool> done(N, false);

    while (processed.size() < N) {
        std::size_t pick = N;
        for (std::size_t i = 0; i < N; ++i) {
            if (!done[i] && inDegree[i] == 0) { pick = i; break; }
        }
        if (pick == N) {
            // Cycle. Find the lowest-indexed undone system and drop its
            // tag-only incoming edges. Read/write edges are preserved —
            // they're load-bearing for memory safety, and rw-only edges
            // are i<j by construction so they cannot form a cycle.
            std::size_t stuck = N;
            for (std::size_t i = 0; i < N; ++i) {
                if (!done[i]) { stuck = i; break; }
            }
            if (stuck == N) break;  // shouldn't happen, but defensive
            std::ostringstream os;
            os << "task graph cycle involving system '"
               << systems_[stuck]->name()
               << "'; dropping its tag-only incoming dependency edges to recover";
            logger().log(LogLevel::Warn, os.str());
            auto& preds = predecessors[stuck];
            std::vector<std::size_t> kept;
            kept.reserve(preds.size());
            for (std::size_t pred : preds) {
                if (pred < stuck && rwConflict(pred, stuck)) {
                    kept.push_back(pred);  // rw-conflict; cannot drop
                } else {
                    // Drop: remove `stuck` from `pred`'s successors list too.
                    auto& s = successors[pred];
                    s.erase(std::remove(s.begin(), s.end(), stuck), s.end());
                }
            }
            preds = std::move(kept);
            inDegree[stuck] = preds.size();
            continue;
        }
        done[pick] = true;
        processed.push_back(pick);
        for (std::size_t succ : successors[pick]) {
            if (inDegree[succ] > 0) inDegree[succ]--;
        }
    }

    // Pack into waves following the topological order.
    for (std::size_t i : processed) {
        std::size_t minWave = 0;
        for (std::size_t pred : predecessors[i]) {
            minWave = std::max(minWave, systemWave_[pred] + 1);
        }
        std::size_t w = minWave;
        while (true) {
            if (w >= waves_.size()) { waves_.emplace_back(); break; }
            bool conflicts = false;
            for (std::size_t j : waves_[w]) {
                if (rwConflict(i, j)) { conflicts = true; break; }
            }
            if (!conflicts) break;
            ++w;
        }
        waves_[w].push_back(i);
        systemWave_[i]      = w;
        systemDependsOn_[i] = predecessors[i];
    }
    (void)edgeIsTagOnly;
}

void EngineImpl::commitBuffer(CommandBuffer& cb) {
    auto& storage = world_.impl_().storage;
    commandsThisStep_ += cb.commands().size();
    for (auto& cmd : cb.commands()) {
        std::visit([&](auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, detail::CmdSpawn>) {
                if (c.reserved.valid()) {
                    // §3.5: spawn into a slot previously taken via
                    // SystemContext::reserveHandle(). Falls back to a
                    // fresh allocation if the reservation was discarded
                    // (e.g. by a competing path that consumed it first).
                    if (!storage.materializeReserved(c.reserved,
                            c.transform, c.velocity, c.render, c.userData,
                            c.acceleration, c.parent,
                            c.health, c.faction, c.animationState,
                            c.physicsBody, c.navAgent, c.boundingVolume,
                            c.initialMask)) {
                        storage.spawn(c.transform, c.velocity,
                                      c.render, c.userData,
                                      c.acceleration, c.parent,
                                      c.health, c.faction, c.animationState,
                                      c.physicsBody, c.navAgent, c.boundingVolume,
                                      c.initialMask);
                    }
                } else {
                    storage.spawn(c.transform, c.velocity,
                                  c.render, c.userData,
                                  c.acceleration, c.parent,
                                  c.health, c.faction, c.animationState,
                                  c.physicsBody, c.navAgent, c.boundingVolume,
                                  c.initialMask);
                }
            } else if constexpr (std::is_same_v<T, detail::CmdDestroy>) {
                storage.destroy(c.entity);
            } else if constexpr (std::is_same_v<T, detail::CmdSetTransform>) {
                if (auto* p = storage.mutTransform(c.entity)) *p = c.value;
            } else if constexpr (std::is_same_v<T, detail::CmdSetVelocity>) {
                if (auto* p = storage.mutVelocity(c.entity))  *p = c.value;
            } else if constexpr (std::is_same_v<T, detail::CmdSetRenderTag>) {
                // Migrate first so the destination chunk has the
                // RenderTag slot, then write the value. The auto-derive
                // matches the legacy "RenderTag iff meshId>=0" rule.
                if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                    ComponentSet newMask = *m;
                    if (c.value.meshId >= 0) newMask.add(Component::RenderTag);
                    else                     newMask.remove(Component::RenderTag);
                    storage.setMaskAndMigrate(c.entity, newMask);
                }
                if (auto* p = storage.mutRenderTag(c.entity)) *p = c.value;
            } else if constexpr (std::is_same_v<T, detail::CmdSetUserData>) {
                if (auto* p = storage.mutUserData(c.entity))  *p = c.value;
            } else if constexpr (std::is_same_v<T, detail::CmdSetAcceleration>) {
                if (auto* p = storage.mutAcceleration(c.entity)) *p = c.value;
            } else if constexpr (std::is_same_v<T, detail::CmdSetParent>) {
                if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                    ComponentSet newMask = *m;
                    if (c.value.parent.valid()) newMask.add(Component::Parent);
                    else                        newMask.remove(Component::Parent);
                    storage.setMaskAndMigrate(c.entity, newMask);
                }
                if (auto* p = storage.mutParent(c.entity)) *p = c.value;
            } else if constexpr (std::is_same_v<T, detail::CmdSetHealth>) {
                // §3.1 batch-5 set* methods: attaching a value attaches
                // the presence bit. Migrate FIRST so the destination
                // archetype has a Health slot to write into.
                if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                    ComponentSet newMask = *m;
                    newMask.add(Component::Health);
                    storage.setMaskAndMigrate(c.entity, newMask);
                }
                if (auto* p = storage.mutHealth(c.entity)) *p = c.value;
            } else if constexpr (std::is_same_v<T, detail::CmdSetFaction>) {
                if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                    ComponentSet newMask = *m;
                    newMask.add(Component::Faction);
                    storage.setMaskAndMigrate(c.entity, newMask);
                }
                if (auto* p = storage.mutFaction(c.entity)) *p = c.value;
            } else if constexpr (std::is_same_v<T, detail::CmdSetAnimationState>) {
                if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                    ComponentSet newMask = *m;
                    newMask.add(Component::AnimationStateRef);
                    storage.setMaskAndMigrate(c.entity, newMask);
                }
                if (auto* p = storage.mutAnimationStateRef(c.entity)) *p = c.value;
            } else if constexpr (std::is_same_v<T, detail::CmdSetPhysicsBody>) {
                if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                    ComponentSet newMask = *m;
                    newMask.add(Component::PhysicsBodyRef);
                    storage.setMaskAndMigrate(c.entity, newMask);
                }
                if (auto* p = storage.mutPhysicsBodyRef(c.entity)) *p = c.value;
            } else if constexpr (std::is_same_v<T, detail::CmdSetNavAgent>) {
                if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                    ComponentSet newMask = *m;
                    newMask.add(Component::NavAgentRef);
                    storage.setMaskAndMigrate(c.entity, newMask);
                }
                if (auto* p = storage.mutNavAgentRef(c.entity)) *p = c.value;
            } else if constexpr (std::is_same_v<T, detail::CmdSetBoundingVolume>) {
                if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                    ComponentSet newMask = *m;
                    newMask.add(Component::BoundingVolume);
                    storage.setMaskAndMigrate(c.entity, newMask);
                }
                if (auto* p = storage.mutBoundingVolume(c.entity)) *p = c.value;
            } else if constexpr (std::is_same_v<T, detail::CmdSetComponentMask>) {
                storage.setMaskAndMigrate(c.entity, c.value);
            } else if constexpr (std::is_same_v<T, detail::CmdAddTag>) {
                if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                    ComponentSet newMask = *m;
                    newMask.add(c.tag);
                    storage.setMaskAndMigrate(c.entity, newMask);
                }
            } else if constexpr (std::is_same_v<T, detail::CmdRemoveTag>) {
                if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                    ComponentSet newMask = *m;
                    newMask.remove(c.tag);
                    storage.setMaskAndMigrate(c.entity, newMask);
                }
            } else if constexpr (std::is_same_v<T, detail::CmdAddUserComponent>) {
                // §3.1 batch 6b: migrate the entity into the
                // destination archetype (which gets a UserComponentColumn
                // for this bit during getOrCreateArchetype) and write
                // the user-supplied blob into the new row. No-op for
                // stale handles.
                if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                    ComponentSet newMask = *m;
                    newMask.add(static_cast<Component>(1ull << c.bit));
                    storage.setMaskAndMigrate(c.entity, newMask);
                    const auto loc = storage.locate(c.entity);
                    auto& chunk = storage.archetypes().chunks()[loc.archetype];
                    if (auto* col = chunk.findUserColumn(c.bit)) {
                        std::memcpy(col->rowPtr(loc.row), c.data(), col->stride);
                    }
                }
            } else if constexpr (std::is_same_v<T, detail::CmdRemoveUserComponent>) {
                // Idempotent: bit absent → migrate is a no-op fast path.
                if (const auto* m = storage.tryGetComponentMask(c.entity)) {
                    ComponentSet newMask = *m;
                    newMask.remove(static_cast<Component>(1ull << c.bit));
                    storage.setMaskAndMigrate(c.entity, newMask);
                }
            }
        }, cmd);
    }
    cb.clear();
}

void EngineImpl::buildRenderFrame() {
    const unsigned back = 1u - frontIndex_.load(std::memory_order_acquire);
    auto& dst = renderInstanceBuffers_[back];
    dst.clear();

    // §3.1 batch 6: walk archetype chunks rather than the legacy
    // stitched dense view — the chunk's mask is checked once per chunk
    // (skipping RenderTag-less and Disabled archetypes wholesale)
    // instead of a per-entity test inside the loop.
    const auto& chunks = world_.impl_().storage.archetypes().chunks();
    std::size_t reserveHint = 0;
    for (const auto& c : chunks) {
        if (!c.mask.has(Component::RenderTag)) continue;
        if (c.mask.has(Component::DisabledTag)) continue;
        reserveHint += c.entities.size();
    }
    dst.reserve(reserveHint);
    for (const auto& c : chunks) {
        if (!c.mask.has(Component::RenderTag)) continue;
        if (c.mask.has(Component::DisabledTag)) continue;
        const bool hasUserData = c.mask.has(Component::UserData);
        const auto rows = c.entities.size();
        for (std::size_t i = 0; i < rows; ++i) {
            dst.push_back(RenderInstance{
                c.entities[i],
                c.transforms[i],
                c.renderTags[i].meshId,
                c.renderTags[i].materialId,
                c.renderTags[i].flags,
                hasUserData ? c.userData[i].value : 0,
            });
        }
    }

    // §3.2 batch 8: merge per-system RenderFrameBuilder slices into the
    // back storage in registration order. The published RenderFrame
    // exposes spans pointing into this storage.
    auto& hier = renderFrameStorage_[back];
    hier.clear();
    for (const auto& builder : systemRenderBuilders_) {
        const auto cams = builder.cameras();
        hier.cameras.insert(hier.cameras.end(), cams.begin(), cams.end());
        const auto lts = builder.lights();
        hier.lights.insert(hier.lights.end(), lts.begin(), lts.end());
        for (std::size_t p = 0; p < kRenderPassCount; ++p) {
            const auto items = builder.drawItems(
                static_cast<RenderPass>(p));
            hier.drawItems[p].insert(hier.drawItems[p].end(),
                                     items.begin(), items.end());
        }
        const auto dl = builder.debugLines();
        hier.debugLines.insert(hier.debugLines.end(), dl.begin(), dl.end());
        const auto dp = builder.debugPoints();
        hier.debugPoints.insert(hier.debugPoints.end(),
                                dp.begin(), dp.end());
        const auto dt = builder.debugText();
        hier.debugText.insert(hier.debugText.end(), dt.begin(), dt.end());
    }

    auto& frame = renderFrames_[back];
    frame.tick = tick_;
    frame.simulationTime = simulationTime_;
    frame.deltaTime = cfg_.fixedStepSeconds;
    frame.alpha = 0.0f;
    frame.instances = std::span<const RenderInstance>(dst.data(), dst.size());
    frame.cameras = std::span<const Camera>(hier.cameras.data(),
                                            hier.cameras.size());
    frame.lights  = std::span<const Light>(hier.lights.data(),
                                           hier.lights.size());
    for (std::size_t p = 0; p < kRenderPassCount; ++p) {
        frame.drawItems[p] = std::span<const DrawItem>(
            hier.drawItems[p].data(), hier.drawItems[p].size());
    }
    frame.debugLines  = std::span<const DebugLine>(
        hier.debugLines.data(), hier.debugLines.size());
    frame.debugPoints = std::span<const DebugPoint>(
        hier.debugPoints.data(), hier.debugPoints.size());
    frame.debugText   = std::span<const DebugText>(
        hier.debugText.data(), hier.debugText.size());

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

    // Pause skips the entire simulation: tick does not advance, systems
    // are not run, no commits. Stats are reset to "this step did nothing"
    // so the HUD doesn't show stale numbers from before the pause.
    if (paused_.load(std::memory_order_acquire)) {
        for (auto& ss : systemStats_) {
            ss.lastUpdateSeconds = 0.0;
            ss.jobsSubmittedLastStep = 0;
            ss.commandsCommittedLastStep = 0;
        }
        stats_.lastStepSeconds = 0.0;
        stats_.jobsSubmittedLastStep = 0;
        stats_.commandsCommittedLastStep = 0;
        stats_.commitDurationSeconds = 0.0;
        return;
    }

    const double dt = cfg_.fixedStepSeconds * timeScale_;
    const auto stepStart = std::chrono::steady_clock::now();

    commandsThisStep_ = 0;
    commitSecondsThisStep_ = 0.0;
    std::uint64_t jobsThisStep = 0;

    // Reset per-system "last step" counters; lifetime totals are preserved.
    for (auto& ss : systemStats_) {
        ss.lastUpdateSeconds = 0.0;
        ss.jobsSubmittedLastStep = 0;
        ss.commandsCommittedLastStep = 0;
        ss.waitSeconds = 0.0;
        ss.peakQueueDepth = 0;
    }

    // §3.1 preStep: serial, registration order, on the sim thread. Hooks
    // run before any wave starts so they can pump per-tick input queues
    // or reset scratch state. Commands emitted via ctx.single() are
    // committed immediately so wave systems observe them.
    for (std::size_t i = 0; i < systems_.size(); ++i) {
        SystemContextImpl ctx(*this, world_, dt, tick_,
                              systemPreferredGrain(i));
        systems_[i]->preStep(ctx);
        const auto commitStart = std::chrono::steady_clock::now();
        for (auto& cb : ctx.buffers()) commitBuffer(cb);
        commitSecondsThisStep_ += std::chrono::duration<double>(
            std::chrono::steady_clock::now() - commitStart).count();
    }

    // §3.5 batch 12: reset the budget flag at step start. Workers see
    // false until we observe an over-budget wave boundary below.
    overBudget_.store(false, std::memory_order_release);

    // §3.5 batch 12: helper — decide whether system `sysIdx` is skipped
    // this tick under the active policy. Returns the reason tag if
    // skipped, or empty if not. Per-wave / pre-update.
    auto skipReason = [this](std::size_t sysIdx) -> std::string_view {
        if (!systems_[sysIdx]->skippable()) return {};
        if (skipPolicy_ == SkipPolicy::Budget) {
            if (overBudget_.load(std::memory_order_acquire)) return "budget";
        } else { // Scripted
            const char* nm = systems_[sysIdx]->name();
            if (!nm) return {};
            const std::string_view view(nm);
            for (const auto& s : scriptedSkips_) {
                if (s.tick == tick_ && s.systemName == view) return "scripted";
            }
        }
        return {};
    };

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
                *this, world_, dt, tick_,
                systemPreferredGrain(wave[k])));
        }

        // Pre-compute skip decisions for this wave. The result drives
        // both `update()` invocation and stats reporting.
        std::vector<std::string_view> waveSkipReason(wave.size());
        for (std::size_t k = 0; k < wave.size(); ++k) {
            waveSkipReason[k] = skipReason(wave[k]);
        }

        // Per-system update durations, captured by whichever thread runs
        // the system. Slot k corresponds to wave[k].
        std::vector<double> updateSeconds(wave.size(), 0.0);
        auto runIndex = [&](std::size_t k) {
            if (!waveSkipReason[k].empty()) {
                updateSeconds[k] = 0.0;
                return;
            }
            const auto t0 = std::chrono::steady_clock::now();
            systems_[wave[k]]->update(*ctxs[k]);
            updateSeconds[k] = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();
        };

        if (wave.size() == 1) {
            runIndex(0);
        } else {
            // Spawn helper threads for all but the last system in the wave;
            // run the tail on this thread to avoid a wasted join.
            std::vector<std::thread> helpers;
            helpers.reserve(wave.size() - 1);
            for (std::size_t k = 0; k + 1 < wave.size(); ++k) {
                helpers.emplace_back([&runIndex, k] { runIndex(k); });
            }
            runIndex(wave.size() - 1);
            for (auto& t : helpers) t.join();
        }

        // Emit SystemSkipped events for any skipped slot. Drained at the
        // tick boundary like every other typed channel.
        if (publicEngine_) {
            for (std::size_t k = 0; k < wave.size(); ++k) {
                if (waveSkipReason[k].empty()) continue;
                SystemSkipped ev;
                ev.tick       = tick_;
                ev.systemName = systems_[wave[k]]->name()
                              ? std::string_view(systems_[wave[k]]->name())
                              : std::string_view();
                ev.reason     = waveSkipReason[k];
                publicEngine_->events<SystemSkipped>().emit(ev);
            }
        }

        // Commit buffers in registration order (wave[] is already in
        // registration order). Sibling systems wrote to disjoint component
        // categories, so commit order among them is observationally a no-op,
        // but we keep it deterministic to make stats and side-effects stable.
        // We bracket each system's commit with a snapshot of commandsThisStep_
        // so per-system command counts come out of the same accumulator.
        for (std::size_t k = 0; k < wave.size(); ++k) {
            const std::uint64_t commandsBefore = commandsThisStep_;
            const auto commitStart = std::chrono::steady_clock::now();
            for (auto& cb : ctxs[k]->buffers()) {
                commitBuffer(cb);
            }
            commitSecondsThisStep_ += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - commitStart).count();
            const std::uint64_t cmds = commandsThisStep_ - commandsBefore;
            const std::uint64_t jobs = ctxs[k]->jobsSubmitted();
            jobsThisStep += jobs;

            auto& ss = systemStats_[wave[k]];
            ss.lastUpdateSeconds = updateSeconds[k];
            ss.jobsSubmittedLastStep = jobs;
            ss.commandsCommittedLastStep = cmds;
            ss.totalJobsSubmitted += jobs;
            ss.totalCommandsCommitted += cmds;
            ss.waitSeconds = ctxs[k]->waitSeconds();
            ss.peakQueueDepth = ctxs[k]->peakQueueDepth();
            // EWMA with alpha = 1/16, matching EngineStats::avgStepSeconds.
            // First sample initializes the average.
            if (stats_.totalTicks == 0) {
                ss.avgUpdateSeconds = ss.lastUpdateSeconds;
            } else {
                constexpr double kEwmaAlpha = 1.0 / 16.0;
                ss.avgUpdateSeconds = ss.avgUpdateSeconds * (1.0 - kEwmaAlpha)
                                    + ss.lastUpdateSeconds * kEwmaAlpha;
            }
        }

        // §3.5 batch 12: post-wave budget check. We sample wall-clock
        // AFTER the commit phase so a wave that overruns hands the next
        // wave a chance to skip its `Skippable` systems. Pure Scripted
        // mode bypasses this — its skip decisions come from the queue,
        // not the clock.
        if (skipPolicy_ == SkipPolicy::Budget && tickBudgetSeconds_ > 0.0) {
            const double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - stepStart).count();
            if (elapsed > tickBudgetSeconds_) {
                overBudget_.store(true, std::memory_order_release);
            }
        }
    }

    // §3.1 postStep: serial, registration order, after every wave's
    // commit. Use it to publish per-tick events or finalize accumulators.
    // Commands emitted here are visible to the next tick's preStep, not
    // to this tick's wave systems.
    for (std::size_t i = 0; i < systems_.size(); ++i) {
        SystemContextImpl ctx(*this, world_, dt, tick_,
                              systemPreferredGrain(i));
        systems_[i]->postStep(ctx);
        const auto commitStart = std::chrono::steady_clock::now();
        for (auto& cb : ctx.buffers()) commitBuffer(cb);
        commitSecondsThisStep_ += std::chrono::duration<double>(
            std::chrono::steady_clock::now() - commitStart).count();
    }

    // §3.3 resource loaders pump after the last postStep commit and
    // before the tick boundary. Loaders are sim-thread-only and own
    // any I/O threads themselves; we just give them their per-tick
    // poll opportunity. Iterating in registration order keeps
    // teardown order well-defined.
    //
    // §3.5 batch 12: call cancel() BEFORE update() so a loader can drop
    // newly-stale requests this same tick rather than waiting another
    // tick. Each loader's `cancel()` and `update()` run on the sim
    // thread back-to-back.
    if (publicEngine_) {
        for (auto& loader : resourceLoaders_) {
            if (!loader) continue;
            loader->cancel(*publicEngine_);
            loader->update(*publicEngine_);
        }
    }

    // §3.5 reaping: any handle that was reserveHandle()-ed but never
    // matched a cb.spawn(handle, ...) is dropped here. Bumps generation
    // so the user's handle stops validating.
    world_.impl_().storage.discardAllReservations();

    // §3.3 drain event channels: writes from this tick flip into the
    // next tick's read buffer so drainTick() sees this tick's events.
    for (auto& [type, entry] : eventChannels_) {
        if (entry.drain) entry.drain(entry.ptr);
    }

    // §3.2 batch 8: render-prep hook. Each system writes its slice into
    // a private RenderFrameBuilder; the engine merges them in
    // registration order during buildRenderFrame() below. Hooks run on
    // the sim thread, single-threaded — every gameplay change has
    // settled by this point.
    for (std::size_t i = 0; i < systems_.size(); ++i) {
        systemRenderBuilders_[i].reset();
        systems_[i]->buildRenderFrame(systemRenderBuilders_[i]);
    }

    tick_++;
    // Wall-clock advances by one fixed step regardless of time scale —
    // only `dt` seen by systems is scaled. Keeps tick_ ↔ simulationTime_
    // a clean integer relationship.
    simulationTime_ += cfg_.fixedStepSeconds;

    buildRenderFrame();
    if (renderer_) {
        const unsigned front = frontIndex_.load(std::memory_order_acquire);
        renderer_->submitFrame(renderFrames_[front]);
    }

    const auto stepEnd = std::chrono::steady_clock::now();
    const double stepSeconds = std::chrono::duration<double>(stepEnd - stepStart).count();

    stats_.tick = tick_;
    stats_.lastStepSeconds = stepSeconds;
    stats_.commitDurationSeconds = commitSecondsThisStep_;
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

void EngineImpl::setTimeScale(double scale) noexcept {
    // Clamp to non-negative — a negative scale would mean "go backwards"
    // which is nonsense for a forward-only fixed-step loop.
    timeScale_ = scale < 0.0 ? 0.0 : scale;
}

void EngineImpl::pushScriptedSkip(std::uint64_t tick,
                                  std::string_view systemName) {
    scriptedSkips_.push_back(ScriptedSkip{tick, std::string(systemName)});
}

void EngineImpl::clearScriptedSkips() noexcept {
    scriptedSkips_.clear();
}

void* EngineImpl::getEventChannelRaw(std::type_index type,
                                     void* (*factory)(),
                                     void (*deleter)(void*),
                                     void (*drainFn)(void*)) {
    auto it = eventChannels_.find(type);
    if (it != eventChannels_.end()) return it->second.ptr;
    EventChannelEntry entry;
    entry.ptr     = factory();
    entry.deleter = deleter;
    entry.drain   = drainFn;
    void* raw = entry.ptr;
    eventChannels_.emplace(type, entry);
    return raw;
}

void EngineImpl::shutdown() {
    if (!initialized_) return;
    initialized_ = false;

    {
        std::ostringstream os;
        os << "engine shutdown after " << stats_.totalTicks
           << " tick(s), "
           << stats_.totalCommandsCommitted << " command(s) committed";
        logger().log(LogLevel::Info, os.str());
    }

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

    // §3.2 batch 7: notify loaders so they can cancel in-flight work
    // before destruction. Reverse-registration order matches the
    // teardown that immediately follows so a loader's onShutdown sees
    // the same engine state its destructor will.
    if (publicEngine_) {
        for (auto it = resourceLoaders_.rbegin();
             it != resourceLoaders_.rend(); ++it) {
            if (*it) (*it)->onShutdown(*publicEngine_);
        }
    }

    // Release resource loaders in reverse-registration order, so a
    // loader that depends on an earlier-registered one tears down
    // first.
    while (!resourceLoaders_.empty()) {
        resourceLoaders_.pop_back();
    }
}

} // namespace threadmaxx::internal
