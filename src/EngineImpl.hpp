#pragma once

#include "JobSystem.hpp"
#include "UserComponentRegistry.hpp"
#include "WorldImpl.hpp"

#include "threadmaxx/CommandBuffer.hpp"
#include "threadmaxx/CommitBreakdown.hpp"
#include "threadmaxx/Config.hpp"
#include "threadmaxx/Logger.hpp"
#include "threadmaxx/RenderFrame.hpp"
#include "threadmaxx/Resource.hpp"
#include "threadmaxx/ScratchArena.hpp"
#include "threadmaxx/SkipPolicy.hpp"
#include "threadmaxx/Stats.hpp"
#include "threadmaxx/System.hpp"
#include "threadmaxx/Tuning.hpp"
#include "threadmaxx/World.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace threadmaxx {
class Engine;
class IRenderer;
class IGame;
class ITraceSink;
class ITuningPolicy;
struct WorldSnapshot;
}

namespace threadmaxx::internal {

// Concrete SystemContext handed to ISystem::update(). It captures everything
// a system needs to schedule parallel work and read the world.
class SystemContextImpl : public SystemContext {
public:
    SystemContextImpl(class EngineImpl& engine, const World& world,
                      const WorldView& view,
                      double dt, std::uint64_t tick,
                      std::uint32_t preferredGrain = 0,
                      std::uint32_t preferredWorkerCap = 0)
        : engine_(engine), world_(world), view_(view), dt_(dt), tick_(tick),
          preferredGrain_(preferredGrain),
          preferredWorkerCap_(preferredWorkerCap) {}

    const World&     world() const noexcept override { return world_; }
    const WorldView& worldView() const noexcept override { return view_; }
    double           dt()    const noexcept override { return dt_; }
    std::uint64_t    tick()  const noexcept override { return tick_; }

    void parallelFor(std::uint32_t count, std::uint32_t grain, JobFn fn) override;
    void parallelFor(std::uint32_t count, std::uint32_t grain, JobFnArena fn) override;
    void parallelFor(std::uint32_t count, std::uint32_t grain, JobFn fn,
                     JobPriority priority) override;
    void parallelFor(std::uint32_t count, std::uint32_t grain, JobFnArena fn,
                     JobPriority priority) override;
    void single(JobFn fn) override;
    void single(JobFnArena fn) override;
    EntityHandle reserveHandle() override;
    std::uint32_t reserveHandles(std::uint32_t count,
                                 std::span<EntityHandle> out) override;
    bool shouldYield() const noexcept override;
    std::uint32_t workerCount() const noexcept override;

    // Per-system list of command buffers, in submission order. The
    // matching ScratchArena (if any) lives in `arenas_[i]` — they grow
    // in lockstep; arenas_[i] is default-constructed (empty) for chunks
    // submitted without arena support.
    std::vector<CommandBuffer>&  buffers() noexcept { return buffers_; }
    std::vector<ScratchArena>&   arenas()  noexcept { return arenas_; }

    // Number of jobs handed to JobSystem from this context. parallelFor
    // chunks count; single() does not (it runs inline).
    std::uint64_t jobsSubmitted() const noexcept { return jobsSubmitted_; }

    // §3.1 SystemStats extras. waitSeconds: cumulative time spent in
    // parallelFor's latch wait — i.e. how long the system's own thread
    // sat blocked waiting for workers. peakQueueDepth: max value of
    // JobSystem::outstanding() sampled right after each parallelFor
    // submit, recording wave congestion the system saw.
    double        waitSeconds()    const noexcept { return waitSeconds_; }
    std::uint32_t peakQueueDepth() const noexcept { return peakQueueDepth_; }

    // ADAPTIVE_TUNING.md T3 — summed wall-clock duration of every
    // sub-job's user lambda dispatched through this context, in
    // nanoseconds. Workers `fetch_add(relaxed)` after each sub-job
    // returns; the sim thread reads it after `done.wait()` so the
    // memory ordering is supplied by the JobLatch acquire-release
    // (the relaxed adds happen-before count_down's release, which
    // happens-before wait's acquire on the reader side).
    std::uint64_t subJobNanosTotal() const noexcept {
        return subJobNanos_.load(std::memory_order_relaxed);
    }

private:
    class EngineImpl& engine_;
    const World&      world_;
    const WorldView&  view_;
    double            dt_;
    std::uint64_t     tick_;
    std::vector<CommandBuffer> buffers_;
    std::vector<ScratchArena>  arenas_;   // parallel to buffers_; same length
    std::uint64_t     jobsSubmitted_ = 0;
    double            waitSeconds_   = 0.0;
    std::uint32_t     peakQueueDepth_ = 0;
    std::uint32_t     preferredGrain_ = 0;
    // ADAPTIVE_TUNING.md T2 — clamp on parallelFor's sub-job count.
    // Zero = uncapped. Sourced from ISystem::preferredWorkerCap() at
    // SystemContextImpl construction; pinned for the wave.
    std::uint32_t     preferredWorkerCap_ = 0;
    // ADAPTIVE_TUNING.md T3 — workers fetch_add(relaxed) here after
    // each sub-job; the sim thread reads it post-wait. Visible
    // ordering is provided by JobLatch's count_down/wait pair.
    std::atomic<std::uint64_t> subJobNanos_{0};

    // SHARDED_OPTIMISATION.md S8 — installs the chunk locator on each
    // freshly-created CommandBuffer in `buffers_[firstIdx..firstIdx+count)`
    // when the engine is configured for sharded commit. The locator
    // closes over `engine_.world().impl_().storage`, which outlives
    // the SystemContext.
    void installLocators(std::size_t firstIdx, std::size_t count) noexcept;
};

// The full engine state. Hidden behind PImpl from the public Engine class.
class EngineImpl {
public:
    explicit EngineImpl(const Config& cfg);
    ~EngineImpl();

    bool initialize(IGame& game, Engine& publicEngine);
    void step();
    void run();
    void shutdown();

    void requestQuit() noexcept { quit_.store(true, std::memory_order_release); }
    bool quitRequested() const noexcept { return quit_.load(std::memory_order_acquire); }

    void registerSystem(std::unique_ptr<ISystem> system);
    std::size_t registerSystemAt(std::size_t position,
                                 std::unique_ptr<ISystem> system);
    std::size_t registeredSystemCount() const noexcept { return systems_.size(); }
    void setRenderer(IRenderer* renderer) noexcept { renderer_ = renderer; }

    // §3.6.5 batch 15a — informational resize forwarding. No-op if no
    // renderer is installed.
    void notifyResize(std::uint32_t width, std::uint32_t height) noexcept;

    // §3.6.5 batch 15a — public-API surface for Engine::workerCount.
    std::uint32_t workerCount() const noexcept;

    void setLogger(ILogger* logger) noexcept { logger_ = logger; }

    // §3.7 batch 14 — telemetry / stall watchdog.
    void   setTraceSink(::threadmaxx::ITraceSink* sink) noexcept { traceSink_ = sink; }
    // ADAPTIVE_TUNING.md T4 — adaptive tuning policy hook-up.
    void           setTuningPolicy(::threadmaxx::ITuningPolicy* p) noexcept {
        tuningPolicy_ = p;
        // ADAPTIVE_TUNING.md T6 — backwards-compat: installing a
        // non-null policy implicitly switches the runtime into
        // Active; detaching (nullptr) reverts to Off. Explicit
        // setTuningMode() calls after this re-override.
        tuningMode_ = (p != nullptr) ? ::threadmaxx::TuningMode::Active
                                     : ::threadmaxx::TuningMode::Off;
        pendingPatch_.reset();
    }
    ::threadmaxx::ITuningPolicy* tuningPolicy() const noexcept {
        return tuningPolicy_;
    }

    // ADAPTIVE_TUNING.md T6 — explicit mode select + trace plumb.
    void setTuningMode(::threadmaxx::TuningMode m) noexcept {
        tuningMode_ = m;
    }
    ::threadmaxx::TuningMode tuningMode() const noexcept {
        return tuningMode_;
    }
    void setTuningTrace(::threadmaxx::TuningTrace* t) noexcept {
        tuningTrace_ = t;
    }
    ::threadmaxx::TuningTrace* tuningTrace() const noexcept {
        return tuningTrace_;
    }
    void   setStallTimeout(double seconds) noexcept;
    double stallTimeout() const noexcept {
        return stallTimeoutSeconds_.load(std::memory_order_relaxed);
    }
    Engine* publicEngine() noexcept { return publicEngine_; }
    ILogger& logger() noexcept { return logger_ ? *logger_ : defaultLogger_; }

    World&        world()        noexcept { return world_; }
    const World&  world() const  noexcept { return world_; }

    // §3.3 resource loaders. The engine pumps update() once per tick
    // after the postStep hooks commit. addLoader returns a non-owning
    // pointer for inspection / tests.
    IResourceLoader* addResourceLoader(std::unique_ptr<IResourceLoader> loader);

    std::size_t resourceLoaderCount() const noexcept {
        return resourceLoaders_.size();
    }

    LoaderStats aggregateLoaderStats() const noexcept;

    void markResourceStale(std::uint32_t index,
                           std::uint32_t generation,
                           std::type_index type);

    bool preloadUntil(std::function<bool()> done,
                      std::chrono::milliseconds timeout);
    const Config& config() const noexcept { return cfg_; }
    std::uint64_t tick()   const noexcept { return tick_; }
    double simulationTime() const noexcept { return simulationTime_; }

    EngineStats stats() const noexcept { return stats_; }
    JobSystemStats jobSystemStats() const noexcept { return jobs_->stats(); }
    std::span<const SystemStats> systemStats() const noexcept {
        return std::span<const SystemStats>(systemStats_.data(),
                                            systemStats_.size());
    }
    // SHARDED_OPTIMISATION.md S0 — per-step Pass A/B/C breakdown.
    CommitBreakdown lastCommitBreakdown() const noexcept { return commitBreakdown_; }

    ResourceRegistry&       resources()       noexcept { return resources_; }
    const ResourceRegistry& resources() const noexcept { return resources_; }

    // §3.1 batch 6b: engine-side mapping of registered user-component
    // POD types to per-type @ref UserComponentId tokens. The
    // @ref ArchetypeTable consults this registry when materializing a
    // chunk's user columns.
    UserComponentRegistry&       userComponents()       noexcept { return userRegistry_; }
    const UserComponentRegistry& userComponents() const noexcept { return userRegistry_; }

    JobSystem& jobs() noexcept { return *jobs_; }

    // Pause / time-scale (§3.4). Time scale multiplies the dt seen by
    // systems; the engine still ticks at fixedStepSeconds wall-clock.
    // Pause makes step()/run() skip the simulation phase entirely.
    void setTimeScale(double scale) noexcept;
    double timeScale() const noexcept { return timeScale_; }
    void setPaused(bool paused) noexcept { paused_.store(paused, std::memory_order_release); }
    bool paused() const noexcept { return paused_.load(std::memory_order_acquire); }

    // §3.5 batch 12 — tick budget + skip policy.
    void   setTickBudget(double seconds) noexcept { tickBudgetSeconds_ = seconds < 0.0 ? 0.0 : seconds; }
    double tickBudget() const noexcept            { return tickBudgetSeconds_; }
    void   setSkipPolicy(SkipPolicy p) noexcept   { skipPolicy_ = p; }
    SkipPolicy skipPolicy() const noexcept        { return skipPolicy_; }
    /// Append a scripted skip entry: `(tick, systemName-view)` —
    /// `name` must reference storage that outlives the queue. We copy
    /// to an internal std::string for safety.
    void pushScriptedSkip(std::uint64_t tick, std::string_view systemName);
    void clearScriptedSkips() noexcept;
    /// Atomic flag mirrored from the budget check; workers poll via
    /// SystemContext::shouldYield(). Reset to false at step() start;
    /// set true once wall-clock exceeds budget.
    bool overBudget() const noexcept { return overBudget_.load(std::memory_order_acquire); }

    // Event-channel access (§3.3). Channels are created on first request,
    // keyed by std::type_index. The returned void* points at a stable
    // EventChannel<T>; callers cast back to the same type.
    //
    // factory: heap-allocates a fresh channel of the matching type.
    // deleter: tears it down at engine destruction.
    // drainFn: called at tick-boundary to swap the front/back buffers.
    void* getEventChannelRaw(std::type_index type,
                             void* (*factory)(),
                             void (*deleter)(void*),
                             void (*drainFn)(void*));

    /// §3.10.3 batch 24 (F8) — accessor for the per-engine serial.
    /// Forwarded to via `Engine::engineSerial()`.
    std::uint64_t engineSerial() const noexcept { return engineSerial_; }

private:
    // Applies a command buffer's commands to the world. Single-threaded.
    void commitBuffer(CommandBuffer& cb);

    // §3.6 batch 13b — Sharded commit. Processes an entire system's
    // buffer set in one pass:
    //   - Pass A (sim thread): build the migrating-entity set.
    //   - Pass B (sim thread, submission order): hash each command;
    //     apply global commands (spawn/destroy/mask-change/migrate-
    //     possible setX/tag ops/user-component ops) immediately;
    //     queue value-only commands (SetTransform, SetVelocity,
    //     SetAcceleration, SetUserData) on non-migrating entities into
    //     per-destination-chunk bins.
    //   - Pass C (workers): apply each chunk bin in submission order
    //     on a separate worker thread. mut*() lookups by entity
    //     handle remain correct even if the entity moved chunks
    //     during pass B (migrate-touched entities are filtered out
    //     by pass A).
    // Guarantees: bit-for-bit identical world state + `commitHash`
    // to the single-threaded `commitBuffer` path on the same input.
    void commitBuffersSharded(std::vector<CommandBuffer>& buffers);

    // Internal helper: apply ONE command's storage mutation. Used by
    // both the single-threaded path (with inline hashing) and the
    // sharded path (hashing handled separately by the classifier).
    // Returns the resulting EntityHandle for `CmdSpawn` (so the
    // global-lane apply can hash it); kInvalidEntity for all other
    // command types.
    EntityHandle applyCommandNoHash(detail::Command& cmd);

    // §3.6 batch 30 — end-of-step per-archetype hash rollup. For each
    // chunk with `hashDirty == true`, recompute its `cachedHash` from
    // the chunk's mask + entity list + dense arrays + user columns
    // (parallel across chunks via `jobs_` when ≥2 dirty chunks). Then
    // walk all chunks sorted by `mask.bits()` and FNV-1a-64-mix their
    // cachedHashes into a fresh running hash. Returns the combined
    // hash to be published as `stats_.commitHash`. Called by `step()`
    // before publishing stats when `!cfg_.legacyCommitHash`.
    std::uint64_t finalizeCommitHash();

    // Build the back render frame from current world state, then publish it.
    void buildRenderFrame();

    // Submit the currently-published front frame with an overridden alpha.
    // Used by run() to deliver interpolation frames between sim steps,
    // without rebuilding the instance array (world state is unchanged
    // between ticks). Safe to call only on the sim thread.
    void submitInterpolatedFrame(float alpha);

    // Recompute waves_ from systems_'s declared read/write sets. Greedy
    // first-fit in registration order, so within a wave the order is also
    // registration order and ordering between waves is a topological sort.
    void rebuildWaves();

    // ADAPTIVE_TUNING.md T4 — drain `pendingPatch_` (if any) and apply
    // each grain override to `systemPreferredGrain_`. Called at the
    // top of `step()` BEFORE preStep so the new grain takes effect on
    // this tick's wave systems. Unknown system names are logged at
    // Warn via `ILogger` and otherwise ignored — silent acceptance
    // would let a typoed policy silently no-op.
    void applyPendingTuningPatch();

    Config cfg_;
    std::unique_ptr<JobSystem> jobs_;

    World world_;

    // §3.6 batch 13c — wave-scoped read-only view, rebuilt before each
    // wave and shared across the wave's SystemContextImpls.
    WorldView worldView_;

    std::vector<std::unique_ptr<ISystem>> systems_;
    // Wave schedule: waves_[w] holds indices into systems_, all of whose
    // declared read/write sets are pairwise non-conflicting. Recomputed when
    // a system is registered. Empty waves never exist.
    std::vector<std::vector<std::size_t>> waves_;
    IRenderer* renderer_ = nullptr;
    IGame*     game_     = nullptr;
    Engine*    publicEngine_ = nullptr;

    // §3.1 logger plumbing. logger_ is the user-installed sink (or null,
    // meaning "fall back to defaultLogger_"). defaultLogger_ writes to
    // std::cerr at Warn+. Engine routes lifecycle and loader messages
    // through whichever is active via the logger() accessor.
    ILogger*      logger_ = nullptr;
    DefaultLogger defaultLogger_;

    // §3.7 batch 14 — telemetry sink + stall watchdog.
    ::threadmaxx::ITraceSink* traceSink_ = nullptr;

    // ADAPTIVE_TUNING.md T4 — adaptive tuning policy and the staged
    // patch returned by its last `propose()` call. The patch is
    // applied at the next tick boundary (before `preStep`); see
    // `applyPendingTuningPatch()`. Non-owning; user retains lifetime.
    ::threadmaxx::ITuningPolicy*               tuningPolicy_ = nullptr;
    std::optional<::threadmaxx::TuningPatch>   pendingPatch_;
    // ADAPTIVE_TUNING.md T6 — runtime mode + replay/record trace.
    // Off matches v1.3 behaviour exactly (single null check per tick).
    // Active records into `tuningTrace_` if attached. Scripted ignores
    // the policy and pulls patches from `tuningTrace_` instead.
    ::threadmaxx::TuningMode                   tuningMode_   =
        ::threadmaxx::TuningMode::Off;
    ::threadmaxx::TuningTrace*                 tuningTrace_  = nullptr;
    // 0.0 = disabled. setStallTimeout() (un)spawns watchdog_ as needed.
    // Atomic so the watchdog thread can poll it concurrently with the
    // sim thread reconfiguring the timeout. Idempotent in effect (just
    // changes the polling cadence), so relaxed ordering is sufficient.
    std::atomic<double>       stallTimeoutSeconds_{0.0};
    std::thread               watchdog_;
    std::atomic<bool>         watchdogRun_{false};
    // Sim thread writes these at step start; watchdog reads.
    std::atomic<std::uint64_t> watchdogStepStartNs_{0};  // steady_clock::now ns
    std::atomic<std::uint64_t> watchdogActiveTick_{0};
    // Per-tick "stall already announced" latch so we don't spam events.
    std::atomic<bool>         watchdogStallEmitted_{false};

    void startWatchdog_();
    void stopWatchdog_();
    void watchdogThreadFn_();

public:
    // §3.9.5 batch 20 — async snapshot writer. The public Engine
    // forwards `snapshotAsync` here; the worker is lazily spawned on
    // the first call and joined in `shutdown`.
    void snapshotAsync(std::function<void(::threadmaxx::WorldSnapshot)> callback);

private:
    std::thread                                snapshotWorker_;
    std::mutex                                 snapshotMtx_;
    std::condition_variable                    snapshotCv_;
    std::deque<std::function<void()>>          snapshotQueue_;
    std::atomic<bool>                          snapshotStop_{false};

    void startSnapshotWorker_();
    void stopSnapshotWorker_();

    // §3.9.6 batch 21 — engine-owned scratch buffers for the sharded
    // commit's classifier passes. Sized lazily; preserved across
    // commits so the steady state pays zero allocations after the
    // first tick.
    std::vector<std::uint8_t>                  shardMigratingBitmap_;
    std::vector<std::uint32_t>                 shardMigratingIndices_;
    std::vector<std::vector<detail::Command*>> shardChunkBins_;

    // SHARDED_OPTIMISATION.md S6 — Engine-owned scratch buffer for the
    // batch-migrate path's per-batch handle collection. `commitBuffer`
    // and `commitBuffersSharded`'s global lane reuse it between batch
    // dispatches so the steady-state pays zero allocations after the
    // first tick. Sim-thread serial. The matching srcRows / dstRows /
    // swaps / perm scratch lives one layer down (`EntityStorage` and
    // `ArchetypeTable` own those).
    std::vector<EntityHandle> batchHandlesScratch_;

    // SHARDED_OPTIMISATION.md S8 — per-buffer demoted-index scratch.
    // A value-only command whose target entity has migrated (either
    // earlier in this step or by a prior buffer's commit this wave)
    // can no longer be safely binned by its record-time chunk hint.
    // We collect those indices here, sort by submission order, and
    // merge-apply them into the global lane.
    std::vector<std::uint32_t> demotedScratch_;

    // SHARDED_OPTIMISATION.md S0 — per-step Pass A/B/C breakdown.
    // Reset to defaults at the top of `step()`; accumulated across all
    // `commitBuffersSharded` calls in the step.
    CommitBreakdown commitBreakdown_;

    std::uint64_t tick_ = 0;
    double simulationTime_ = 0.0;

    std::atomic<bool> quit_{false};
    bool initialized_ = false;

    // Pause / time-scale state (§3.4). timeScale_ is only mutated on the
    // sim thread; paused_ is atomic so any thread can flip it (parallels
    // requestQuit()).
    double            timeScale_ = 1.0;
    std::atomic<bool> paused_{false};

    // Type-erased event channel store (§3.3). Owned vector of unique_ptrs
    // gives stable addresses; the type_index map is a lookup index.
    // Drained between steps (preStep → emits stay live, drain happens on
    // tick boundary after postStep).
    struct EventChannelEntry {
        void* ptr               = nullptr;
        void  (*deleter)(void*) = nullptr;
        void  (*drain)(void*)   = nullptr;
    };
    std::unordered_map<std::type_index, EventChannelEntry> eventChannels_;
    // §3.7 batch 14 — worker jobs and the stall watchdog thread can
    // both trigger first-instantiation of an event channel via
    // `Engine::events<T>()`. Serialize the map mutation; lookup-hit
    // is the steady-state hot path and the mutex is uncontended.
    mutable std::mutex eventChannelsMtx_;

    // §3.10.3 batch 24 (F8) — unique non-zero serial assigned at
    // construction from a global atomic counter. `Engine::events<T>()`
    // uses it as the validity key for its `thread_local` channel cache:
    // a fresh engine that lands at the same memory address as a
    // destroyed one has a different serial, so the cache invalidates
    // automatically and there's no UAF risk. Read via the public
    // `Engine::engineSerial()` getter; never reused.
    std::uint64_t engineSerial_ = 0;

    // Double-buffered render-frame storage. We build into back_, then
    // atomically publish the pointer; the renderer reads through front_.
    std::array<std::vector<RenderInstance>, 2> renderInstanceBuffers_;
    // §3.6.5 batch 15a — previous-tick transform snapshot, paired with
    // each `instances` entry. Index i in renderInstancePrev_[back] is
    // the previous transform for renderInstanceBuffers_[back][i]. The
    // lookup keys off the persistent `prevTransformMap_` which is
    // populated at the END of each `buildRenderFrame` from the just-
    // built `instances` array — i.e. the map describes the just-
    // published frame, ready to be consumed as "prev" on the next
    // tick.
    std::array<std::vector<RenderInstancePrev>, 2> renderInstancePrev_;
    // §3.10.2 batch 22 — F7 fix. Was `unordered_map<EntityHandle,
    // Transform>` cleared + rebuilt each tick (allocator churn at 60
    // Hz). Now a flat vector keyed by `EntityHandle::index`, with a
    // parallel generation vector for the (index, generation) sentinel
    // check. Steady state pays zero allocations after the first frame.
    std::vector<Transform>     prevTransformByIndex_;
    std::vector<std::uint32_t> prevTransformGenByIndex_;
    // §3.2 batch 8: per-frame merged storage backing the hierarchical
    // RenderFrame spans (cameras, lights, per-pass draw items, debug
    // geometry). Double-buffered alongside renderInstanceBuffers_; the
    // RenderFrame::* spans point into renderFrameStorage_[back] during
    // build, and are read by the renderer through renderFrames_[front].
    struct HierarchicalRenderStorage {
        std::vector<Camera>      cameras;
        std::vector<Light>       lights;
        std::array<std::vector<DrawItem>, kRenderPassCount> drawItems;
        std::vector<DebugLine>   debugLines;
        std::vector<DebugPoint>  debugPoints;
        std::vector<DebugText>   debugText;
        void clear() noexcept {
            cameras.clear();
            lights.clear();
            for (auto& bin : drawItems) bin.clear();
            debugLines.clear();
            debugPoints.clear();
            debugText.clear();
        }
    };
    std::array<HierarchicalRenderStorage, 2> renderFrameStorage_;
    std::array<RenderFrame, 2> renderFrames_;
    std::atomic<unsigned> frontIndex_{0};

    // §3.2 batch 8: per-system RenderFrameBuilder, parallel to systems_.
    // Persisted across ticks so the inner std::vector allocations are
    // reused — steady-state usage pays zero allocations after the first
    // tick. Cleared at the start of each tick before invoking each
    // system's buildRenderFrame hook.
    std::vector<RenderFrameBuilder> systemRenderBuilders_;

    // Wall-clock pacing.
    std::chrono::steady_clock::time_point lastIterationTime_{};
    double accumulatedTime_ = 0.0;

    EngineStats stats_;
    std::uint64_t commandsThisStep_ = 0;  // accumulated across commitBuffer calls
    double        commitSecondsThisStep_ = 0.0;  // accumulated in step() across waves

    // §3.6 batch 13a — Running FNV-1a-64 hash fed by every applied
    // mutation in commitBuffer. Reset to the offset basis at step
    // start; published to stats_.commitHash at step end. See
    // `EngineStats::commitHash` for the semantics.
    std::uint64_t commitHashAcc_ = 0xcbf29ce484222325ull;

    // Per-system snapshot, one entry per registered system in registration
    // order. Grown by registerSystem; updated at the end of each step().
    std::vector<SystemStats> systemStats_;

    // Engine-owned, thread-safe registry for game-side resources (meshes,
    // textures, audio clips, ...). Lifetime matches the engine.
    ResourceRegistry resources_;

    // §3.3 registered resource loaders. Pumped after the postStep hook
    // commits. Torn down in reverse-registration order during shutdown.
    std::vector<std::unique_ptr<IResourceLoader>> resourceLoaders_;

    // §3.1 batch 6b: user-component type registry. Lifetime matches the
    // engine. World's ArchetypeTable holds a non-owning pointer to it
    // (wired in `initialize`) so new chunks can look up strides.
    UserComponentRegistry userRegistry_;

    // §3.5 batch 12 — tick budget & skip policy state.
    double             tickBudgetSeconds_ = 0.0;   // 0 == no budget
    std::atomic<bool>  overBudget_{false};
    SkipPolicy         skipPolicy_ = SkipPolicy::Budget;
    struct ScriptedSkip { std::uint64_t tick; std::string systemName; };
    std::vector<ScriptedSkip> scriptedSkips_;

public:
    // §3.4 batch 11: per-system wave + predecessor snapshot. Parallel to
    // `systems_` (same index ordering). Updated by `rebuildWaves`;
    // exposed read-only via the public `taskGraphSnapshot` API.
    const std::vector<std::size_t>& systemWaves() const noexcept {
        return systemWave_;
    }
    const std::vector<std::vector<std::size_t>>& systemDeps() const noexcept {
        return systemDependsOn_;
    }
    // §3.4 batch 11: per-system preferred grain. Looked up by
    // SystemContextImpl::pickGrain when the user passes grain=0.
    std::uint32_t systemPreferredGrain(std::size_t i) const noexcept {
        return i < systemPreferredGrain_.size() ? systemPreferredGrain_[i] : 0u;
    }
    // ADAPTIVE_TUNING.md T2 — per-system parallelFor sub-job cap.
    // Sourced from ISystem::preferredWorkerCap() at registerSystem time.
    std::uint32_t systemPreferredWorkerCap(std::size_t i) const noexcept {
        return i < systemPreferredWorkerCap_.size() ? systemPreferredWorkerCap_[i] : 0u;
    }

private:
    std::vector<std::size_t>              systemWave_;
    std::vector<std::vector<std::size_t>> systemDependsOn_;
    std::vector<std::uint32_t>            systemPreferredGrain_;
    std::vector<std::uint32_t>            systemPreferredWorkerCap_;
};

} // namespace threadmaxx::internal
