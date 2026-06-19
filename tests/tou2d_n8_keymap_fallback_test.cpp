// tou2d_n8_keymap_fallback_test — N8 (2026-06-19) regression pins.
//
// N7 shipped with two production-blocking bugs:
//
//   1. The default-constructed `Settings{}.controls` is an all-zero
//      KeyMap (every binding == kKeyUnbound). When `loadSettings`
//      returned false (missing settings.dat) the host's working copy
//      stayed at that all-zero state, and `TouGame::onSetup`
//      unconditionally installed it into InputSystem — silently
//      un-binding every key. Players couldn't move.
//
//   2. The N7 stuck detector reverse-thrusted for 30 ticks then handed
//      control back to the engage logic, which immediately re-aimed
//      and thrusted back into the same wall — the visible "wobble"
//      cycle in the 63-bot procedural benchmark.
//
// This test pins:
//
//   * `makeDefaultKeyMap()` returns a non-empty map (at least one
//     slot has a real binding). Trivially true today, but pinning
//     guards against a refactor that empties the table.
//   * `BotConfig` carries the N7 stuck tunables AND the N8 fix
//     persists the per-slot `forceWanderTicks_` counter (proxied via
//     a public-state probe — the counter is private so we exercise it
//     by calling the system's `update` loop indirectly is overkill;
//     instead we pin the public `BotConfig` shape so callers can rely
//     on it).

#include "Check.hpp"

#include "../examples/tou2d/BotControlSystem.hpp"
#include "../examples/tou2d/DemoTypes.hpp"
#include "../examples/tou2d/InputSystem.hpp"
#include "../examples/tou2d/Settings.hpp"

#include <cstddef>

int main() {
    using tou2d::BotConfig;
    using tou2d::BotControlSystem;
    using tou2d::BotDifficulty;
    using tou2d::KeyMap;
    using tou2d::Settings;
    using tou2d::UserComponentIds;
    using tou2d::kActionCount;
    using tou2d::kKeyUnbound;
    using tou2d::kMaxHumans;
    using tou2d::makeDefaultKeyMap;

    // ---- (1) Default-constructed Settings has an EMPTY keymap --------
    // This is the precondition that made the player-can't-move bug
    // possible — pin it explicitly so a future maintainer who notices
    // the empty default doesn't "fix" it without also fixing the
    // upstream wiring that depended on it being the sentinel value.
    {
        Settings s{};
        bool anyBound = false;
        for (std::size_t slot = 0; slot < kMaxHumans && !anyBound; ++slot) {
            for (std::size_t a = 0; a < kActionCount; ++a) {
                if (s.controls.binding[slot][a] != kKeyUnbound) {
                    anyBound = true;
                    break;
                }
            }
        }
        CHECK(!anyBound);  // default Settings{} HAS NO BINDINGS
    }

    // ---- (2) makeDefaultKeyMap() HAS bindings -----------------------
    // The N8 fix in main.cpp pre-fills `settings.controls` from this
    // function BEFORE calling loadSettings. Pin that the function
    // returns at least one real binding so the pre-fill produces a
    // playable keymap.
    {
        const KeyMap km = makeDefaultKeyMap();
        // Slot 0 = P1 = arrow keys + RShift / RCtrl / Slash. The
        // arrow-key constants are guaranteed nonzero by GLFW, so
        // checking "Thrust" (= UP arrow) is the canonical probe.
        const auto thrustKey = km.binding[0][static_cast<std::size_t>(tou2d::Action::Thrust)];
        CHECK(thrustKey != kKeyUnbound);
        // Pin a second slot to guard against a refactor that only
        // populates slot 0.
        const auto p2Thrust  = km.binding[1][static_cast<std::size_t>(tou2d::Action::Thrust)];
        CHECK(p2Thrust       != kKeyUnbound);
    }

    // ---- (3) N7+N8 BotConfig stuck tunables present + sane ----------
    // The N8 fix adds the `forceWanderTicks_` member (private) but
    // relies on `BotConfig::unstuckCommitTicks` to size the wander
    // lock. Re-pin the tunable bands so a future config change can't
    // accidentally zero them (which would break stuck recovery).
    {
        for (int d = 0; d < static_cast<int>(BotDifficulty::Count); ++d) {
            const auto& c = tou2d::botConfigForDifficulty(
                static_cast<BotDifficulty>(d));
            CHECK(c.unstuckWindowTicks > 0);
            CHECK(c.unstuckWindowTicks < 600);     // < 10 s window
            CHECK(c.unstuckMinDispWU   > 0.0f);
            CHECK(c.unstuckMinDispWU   < 100.0f);  // < ~5 ships of motion
            CHECK(c.unstuckCommitTicks > 0);
            CHECK(c.unstuckCommitTicks < 240);     // < 4 s reverse
        }
    }

    // ---- (4) BotControlSystem constructs without crash --------------
    // N8 adds a new `forceWanderTicks_` array; default-construction
    // must zero it (no random "bot starts in forced wander" surprise).
    // We can't read the private state, but the system being valid +
    // a config probe is enough to pin that the type still compiles
    // and the ctor doesn't UB.
    {
        UserComponentIds ids{};
        BotControlSystem sys(ids);
        CHECK(sys.config().unstuckCommitTicks > 0);
    }

    // ---- (5) kMaxPlayerSlots accommodates the largest match --------
    // N8.3 — the bot target-search scratch array `live` is sized to
    // `kMaxPlayerSlots`. The cap MUST cover the largest legal match
    // (4 humans + 63 bots = 67) so every ship is a candidate target.
    // Pre-N8.3 the array was hard-sized at 16, hiding ~51 ships from
    // each bot's target search in a max-player match — the dominant
    // cause of the "bots wander in place" playtest signal.
    {
        CHECK(tou2d::kMaxPlayerSlots >= 67);
    }

    EXIT_WITH_RESULT();
}
