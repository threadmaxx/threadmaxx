#pragma once

#include <cstdint>
#include <string_view>

namespace threadmaxx {

/// §3.5 batch 12 — controls how the engine decides which systems to
/// skip when the tick budget is exceeded.
///
/// @ref Budget is the default. The engine compares wall-clock elapsed
/// against `setTickBudget` at wave boundaries and skips subsequent
/// `Skippable` systems when over-budget. Behavior is local to the
/// machine — across two peers running the same simulation, the skip
/// decisions can diverge based on instantaneous CPU pressure. Fine for
/// single-player; not safe for lockstep networked games.
///
/// @ref Scripted is the deterministic-replay path. The engine ignores
/// the tick budget entirely and consults a per-(tick, systemName)
/// queue populated via @ref Engine::pushScriptedSkip. A system is
/// skipped iff a matching entry exists in the queue. Authoritative
/// servers run @ref Budget locally, drain the `SystemSkipped` event
/// channel, broadcast the decision log; clients replay it via
/// @ref Scripted and produce a byte-identical world state.
enum class SkipPolicy : std::uint8_t {
    Budget   = 0,
    Scripted = 1,
};

/// §3.5 batch 12 — event emitted whenever the engine skips a system.
///
/// `tick` is the tick the skip applied to; `systemName` is the
/// `ISystem::name()` pointer (the engine does not own the string —
/// `name()` is required to return a stable pointer per the existing
/// contract); `reason` is a short tag identifying which policy
/// triggered the skip.
///
/// Subscribe via `engine.events<SystemSkipped>()`. The event channel
/// is drained at the tick boundary like every other typed channel —
/// the authoritative server captures one tick's events before the
/// next, builds the broadcast log, and replays it client-side via
/// @ref Engine::pushScriptedSkip.
struct SystemSkipped {
    std::uint64_t    tick       = 0;
    std::string_view systemName;
    std::string_view reason;     ///< "budget" or "scripted".
};

} // namespace threadmaxx
