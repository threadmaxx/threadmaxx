#pragma once

#include "Stats.hpp"

#include <ostream>
#include <span>

namespace threadmaxx {

/// @file Trace.hpp
/// Tagged snapshot record of one tick's instrumentation, plus a
/// one-line JSON-Lines serializer.
///
/// @ref Engine::frameSnapshot returns a @ref FrameSnapshot describing the
/// most recent step: @ref EngineStats, per-system @ref SystemStats and a
/// @ref JobSystemStats aggregate. The values are the same ones
/// `Engine::stats()`, `Engine::systemStats()` and
/// `Engine::jobSystemStats()` would return; bundling them keeps the
/// caller's record consistent (no risk of one stat lagging another by
/// a step).
///
/// @ref writeJsonLines emits one line per snapshot using the same field
/// names you see on the stats structs. Pipe that into a Chrome-trace
/// converter, Tracy adapter, or your own ingest stack. The serializer
/// has no third-party deps and never allocates beyond the underlying
/// stream's buffer.

/// One frame's worth of instrumentation, returned by
/// @ref Engine::frameSnapshot.
///
/// @warning `systems` points into engine-owned memory and is invalidated
///          by `registerSystem`, `registerSystemAt`, and `shutdown`. Copy
///          the elements out if you need to retain them across calls.
struct FrameSnapshot {
    EngineStats                  engine;
    std::span<const SystemStats> systems;
    JobSystemStats               jobs;
};

/// Serialize a snapshot as one JSON object terminated by `'\n'`.
///
/// The shape is:
/// @code
/// {"tick":N,"step_s":...,"avg_step_s":...,"commit_s":...,
///  "jobs":...,"commands":...,"alive":...,
///  "systems":[{"name":"...","update_s":...,"avg_update_s":...,
///              "jobs":...,"commands":...,"wait_s":...,
///              "peak_queue_depth":...},...],
///  "job_pool":{"total_jobs":...,"own_pops":...,"steals":...,
///              "workers":...,"hist":[...]}}
/// @endcode
/// Field names match the @ref EngineStats / @ref SystemStats /
/// @ref JobSystemStats members so a downstream parser can mechanically
/// map them. `\n`-terminated so multiple snapshots can be appended to
/// the same stream as JSON Lines.
void writeJsonLines(std::ostream& os, const FrameSnapshot& snap);

/// Streaming adapter that emits one snapshot at a time as a sequence of
/// records in the Chrome Trace Event format
/// (`chrome://tracing` / Perfetto). Construct on a stream; the ctor
/// writes the opening `[`, every call to @ref emit appends one
/// `{ph:"X",...}` record per system in the snapshot (plus one for the
/// step as a whole), and the dtor writes the closing `]`. The output
/// is a valid JSON array of duration events.
///
/// Timestamps are in microseconds relative to construction (Chrome
/// trace convention). The `tid` (thread id) field is filled with a
/// per-system stable hash so multiple snapshots line up on the same
/// row per system; `pid` is always 1.
///
/// @code
/// std::ofstream trace("trace.json");
/// threadmaxx::ChromeTraceWriter w(trace);
/// for (int i = 0; i < 600; ++i) {
///     engine.step();
///     w.emit(engine.frameSnapshot());
/// }
/// // w destructor closes the array
/// @endcode
///
/// @par Output reuse
///      `ChromeTraceWriter` is one-shot. Move-only — construct a fresh
///      one for a new file. The destructor writes the closer
///      unconditionally; do not let exceptions propagate through the
///      `emit` loop unless you are okay with a truncated trace (still
///      valid JSON, just missing later events).
class ChromeTraceWriter {
public:
    explicit ChromeTraceWriter(std::ostream& os);
    ~ChromeTraceWriter();

    ChromeTraceWriter(const ChromeTraceWriter&) = delete;
    ChromeTraceWriter& operator=(const ChromeTraceWriter&) = delete;

    /// Append one snapshot's events. Emits one record per system plus
    /// one record describing the whole step (`name:"step"`).
    void emit(const FrameSnapshot& snap);

private:
    std::ostream* os_;
    bool          firstRecord_ = true;
    // Wall-clock cursor in microseconds. We use a monotonically
    // increasing fake timeline (each emit's events placed back-to-back
    // by their measured durations) rather than relying on engine tick
    // timestamps — the snapshot doesn't carry a wall-clock anchor.
    double        cursorMicros_ = 0.0;
};

} // namespace threadmaxx
