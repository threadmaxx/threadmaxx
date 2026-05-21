#pragma once

#include "Engine.hpp"
#include "System.hpp"
#include "Trace.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace threadmaxx {

/// §3.7 batch 14 — telemetry sink: streaming per-tick consumer.
///
/// The engine calls @ref onFrame once per @ref Engine::step (on the
/// simulation thread, after the frame has been built and published) with
/// a finalized @ref FrameSnapshot. The sink owns any buffering and any
/// off-thread I/O; @ref onFrame must be **non-blocking and cheap** —
/// budget it at a few microseconds. Treat the snapshot as borrowed:
/// `snap.systems` is a span into engine-owned memory and is only valid
/// for the call's duration.
///
/// Install via @ref Engine::setTraceSink. The default is `nullptr`
/// (no sink, zero cost). The engine never takes ownership; the sink
/// must outlive the engine (mirror of `setRenderer` / `setLogger`).
class ITraceSink {
public:
    virtual ~ITraceSink() = default;
    /// Called once per `Engine::step` on the simulation thread with a
    /// finalized snapshot. See the class brief for the timing contract
    /// and the borrowed-span lifetime rule.
    virtual void onFrame(const FrameSnapshot& snap) = 0;
    /// Engine-internal: called once on the sim thread at
    /// `Engine::shutdown` before the engine destroys its sink pointer.
    /// Default no-op. Use it to flush in-flight writes / join worker
    /// threads owned by the sink.
    virtual void onShutdown() {}
};

/// §3.7 batch 14 — rolling Chrome-trace JSON sink.
///
/// Each call to @ref onFrame appends one `step` record and one record
/// per system in the snapshot. When the active file exceeds
/// @ref Config::rotationBytes the sink closes it (writing the closing
/// `]`), increments a rotation index, and opens the next file. Files
/// are standalone valid Chrome trace JSON; load any one of them in
/// `chrome://tracing` or Perfetto.
///
/// Path format: `pathTemplate` may contain `%N` which gets replaced
/// with the current rotation index. If no `%N` is present the sink
/// appends `.N.json` to the template. Examples:
///
///     "trace.%N.json"        → trace.0.json, trace.1.json, ...
///     "/var/log/trace"       → /var/log/trace.0.json, ...
///
/// All I/O happens on the sim thread inside @ref onFrame; the sink
/// is not async. If your `rotationBytes` is tiny and your tick
/// budget tight, prefer @ref HudTraceSink or a custom async sink.
///
/// @par Failure mode (clarified, §3.6.5 batch 15b)
///      If the path can't be opened (filesystem error, permission
///      denied, parent directory missing) the sink silently no-ops
///      its writes: subsequent `onFrame` calls do nothing and
///      @ref bytesWrittenCurrent stays at zero. The engine never
///      crashes on a bad path. Game code that needs failure
///      visibility should check `bytesWrittenCurrent()` after a few
///      ticks and react if it's still 0.
class FileTraceSink : public ITraceSink {
public:
    struct Config {
        std::string   pathTemplate    = "trace.%N.json";
        std::size_t   rotationBytes   = 64u * 1024u * 1024u;  // 64 MiB
    };

    /// Construct with the given path template + rotation budget. The
    /// first file is opened lazily on the first @ref onFrame call.
    explicit FileTraceSink(Config cfg);
    ~FileTraceSink() override;

    FileTraceSink(const FileTraceSink&) = delete;
    FileTraceSink& operator=(const FileTraceSink&) = delete;

    void onFrame(const FrameSnapshot& snap) override;
    void onShutdown() override;

    /// §3.9.5 batch 20 — opt-in background-thread mode. When `true`,
    /// `onFrame` copies the snapshot's headline data into an internal
    /// queue and returns immediately; a dedicated writer thread drains
    /// the queue and performs the actual file I/O. When `false`
    /// (default), `onFrame` writes synchronously on the calling
    /// (typically sim) thread — bit-for-bit the pre-batch-20 behavior.
    ///
    /// Toggle once at setup; the call is not safe to interleave with
    /// `onFrame` from another thread. Setting `true` spawns the
    /// writer thread; setting back to `false` joins it. Toggling
    /// flushes any queued work before changing modes.
    ///
    /// @par Lifetime
    ///      The writer thread is joined on destructor / `onShutdown`,
    ///      whichever fires first.
    void setAsync(bool enable);

    /// True iff @ref setAsync(true) is active and the writer thread
    /// is running.
    bool isAsync() const noexcept;

    /// 0-based rotation index of the currently-open file.
    std::uint32_t rotationIndex() const noexcept { return rotationIndex_; }
    /// Bytes written into the current file (approximate; updated after
    /// each `onFrame`).
    std::size_t bytesWrittenCurrent() const noexcept { return bytesCurrent_; }

private:
    struct Impl;
    /// §3.9.5 batch 20 — single synchronous write step. Shared by the
    /// legacy sync `onFrame` path AND the async writer thread spawned
    /// by `setAsync(true)`. Touches `impl_`, `rotationIndex_`, and
    /// `bytesCurrent_`; not safe to call without the queue lock held
    /// in async mode.
    void writeSyncLocked(const FrameSnapshot& snap);

    std::unique_ptr<Impl> impl_;
    Config        cfg_;
    std::uint32_t rotationIndex_ = 0;
    std::size_t   bytesCurrent_  = 0;
};

/// §3.7 batch 14 — single-writer / single-reader latest-snapshot sink.
///
/// Stores only the most recent frame's headline numbers in a seqlock-
/// protected POD. The render thread or HUD reader polls
/// @ref tryGet from any thread and gets a torn-write-free copy of
/// the latest values. There is no buffering — a slow reader sees a
/// stale snapshot, never a half-updated one.
class HudTraceSink : public ITraceSink {
public:
    struct LatestTelemetry {
        std::uint64_t tick                = 0;
        double        lastStepSeconds     = 0.0;
        double        avgStepSeconds      = 0.0;
        double        commitDurationSecs  = 0.0;
        std::uint64_t jobsSubmittedLastStep   = 0;
        std::uint64_t commandsCommittedLastStep = 0;
        std::size_t   aliveEntities       = 0;
        std::uint64_t commitHash          = 0;
        std::uint32_t workerCount         = 0;
        std::uint64_t totalJobs           = 0;
        std::uint64_t totalCommands       = 0;
    };

    HudTraceSink() = default;
    ~HudTraceSink() override = default;

    /// Updates the latest-snapshot bracket using a seqlock pattern. See
    /// the class brief for the writer/reader protocol.
    void onFrame(const FrameSnapshot& snap) override;

    /// Read the latest snapshot. Returns `true` and writes `out` when
    /// a snapshot has been published; returns `false` if @ref onFrame
    /// has never been called. Lock-free under the C++ memory model;
    /// safe to call from any thread.
    bool tryGet(LatestTelemetry& out) const noexcept;

private:
    // Seqlock: odd sequence = writer in progress; even = stable.
    // Writer (sim thread): seq → odd → write data → seq → even.
    // Reader (any thread): re-read until two even reads bracket the data.
    alignas(64) std::atomic<std::uint32_t> seq_{0};
    LatestTelemetry                        data_{};
};

/// §3.7 batch 14 — event emitted when the engine's last `step()` wall
/// clock exceeded the watched target. Subscribe via
/// `engine.events<BudgetExceeded>().subscribeScoped(...)`.
struct BudgetExceeded {
    std::uint64_t tick                = 0;
    double        lastStepSeconds     = 0.0;
    double        targetSeconds       = 0.0;
};

/// §3.7 batch 14 — built-in ISystem that polls
/// @ref EngineStats::lastStepSeconds and emits @ref BudgetExceeded on
/// every tick that exceeds the configured target.
///
/// Register like any other system. The watcher reads stats in
/// `postStep`, after `EngineStats` is published, so it observes the
/// just-finished tick — game code reacts on the *next* tick's
/// `EventChannel<BudgetExceeded>::drainTick`.
///
/// Reads / writes: empty. The watcher does not contend for component
/// access and lands in any wave.
class FrameBudgetWatcher : public ISystem {
public:
    /// @param targetSeconds Hard target wall-clock per `step()`.
    ///        Recommended: slightly under @ref Config::fixedStepSeconds.
    FrameBudgetWatcher(Engine* engine, double targetSeconds);
    ~FrameBudgetWatcher() override = default;

    const char*  name()   const noexcept override { return "frame-budget-watcher"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }
    void         update(SystemContext&) override {}
    void         postStep(SystemContext&) override;

    /// Target passed to the constructor; pinned for the watcher's lifetime.
    double targetSeconds() const noexcept { return target_; }
    /// Number of @ref BudgetExceeded events emitted since construction.
    std::uint64_t exceedCount() const noexcept { return exceedCount_; }

private:
    Engine*       engine_;
    double        target_;
    std::uint64_t exceedCount_ = 0;
};

/// §3.7 batch 14 — event emitted by the engine's stall watchdog when a
/// tick has been running longer than @ref Engine::setStallTimeout.
/// Drains on the sim thread like any other typed event channel; the
/// watchdog itself runs on its own thread.
struct EngineStall {
    /// Tick that was running when the stall was detected. Use it to
    /// correlate with logs.
    std::uint64_t tick = 0;
    /// Wall-clock duration the tick had been running at detection time.
    double durationSeconds = 0.0;
};

} // namespace threadmaxx
