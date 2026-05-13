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
///              "jobs":...,"commands":...},...],
///  "job_pool":{"total_jobs":...,"own_pops":...,"steals":...,"workers":...}}
/// @endcode
/// Field names match the @ref EngineStats / @ref SystemStats /
/// @ref JobSystemStats members so a downstream parser can mechanically
/// map them. `\n`-terminated so multiple snapshots can be appended to
/// the same stream as JSON Lines.
void writeJsonLines(std::ostream& os, const FrameSnapshot& snap);

} // namespace threadmaxx
