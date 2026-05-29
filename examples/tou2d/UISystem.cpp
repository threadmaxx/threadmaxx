#include "UISystem.hpp"

#include "ProceduralLevel.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/System.hpp>

#include <array>
#include <cstdio>
#include <cstring>

namespace tou2d {

namespace {

// MainMenu rows — order matches the §M6 spec. "Continue" is greyed
// until M6.4 wires up the in-flight match resume hook.
constexpr MenuRow kMainMenuRows[] = {
    { "Continue",         MenuAction::Continue,    false },
    { "Single Match",     MenuAction::SingleMatch, true  },
    { "Level Setup",      MenuAction::LevelSetup,  true  },
    { "Options",          MenuAction::Options,     true  },
    { "Benchmark / Stress", MenuAction::Benchmark, true  },
    { "Credits / About",  MenuAction::Credits,     true  },
    { "Quit",             MenuAction::Quit,        true  },
};

// Credits screen — single Back row. Real content lands in M6.6 (or
// later); this lets the navigation contract be exercised today.
constexpr MenuRow kCreditsRows[] = {
    { "tou2d — threadmaxx ECS demo (M6.1)", MenuAction::None,       false },
    { "Back",                                MenuAction::BackToMain, true  },
};

// M6.4 — Pause screen rows. Triggered while gameplay is active by the
// Pause action edge (default Escape; bound in InputSystem's KeyMap).
// On enter the host's `engine.paused()` bind freezes the sim — the
// renderer keeps re-publishing the last submitted frame so paused
// gameplay stays visible behind the menu (no explicit snapshot capture
// needed because `engine.step()` is a no-op while paused). On Resume
// the menu dismisses, the bind unfreezes, the sim continues from
// EXACTLY where it left off (replay-bit-identical to an unpaused match
// where the pause never happened — see Replay record/play paused-skip
// gating in main.cpp).
//
// Restart match / Return to main menu are sticky flags read by the
// host: M6.4 first-cut logs the flag and dismisses the menu; the full
// engine-restart-with-MatchSetup wiring lands in a focused follow-up
// (same posture as M6.2 StartMatch).
constexpr MenuRow kPauseRows[] = {
    { "Resume",              MenuAction::Resume,           true },
    { "Restart match",       MenuAction::RestartMatch,     true },
    { "Options",             MenuAction::Options,          true },
    { "Level setup",         MenuAction::LevelSetup,       true },
    { "Return to main menu", MenuAction::ReturnToMainMenu, true },
    { "Quit",                MenuAction::Quit,             true },
};

// M6.2 — MatchSetup screen. Scroller rows bind 1:1 to MatchSetupKnob
// values; the trailing two Action rows (Start, Back) fire the standard
// MenuAction transitions. Order is the on-screen vertical order so the
// test pinning row indices is the source of truth.
constexpr MenuRow kMatchSetupRows[] = {
    { "Humans",            MenuAction::None,       true,
      MenuRowKind::Scroller, MatchSetupKnob::Humans      },
    { "Bots",              MenuAction::None,       true,
      MenuRowKind::Scroller, MatchSetupKnob::Bots        },
    { "Mode",              MenuAction::None,       true,
      MenuRowKind::Scroller, MatchSetupKnob::Mode        },
    { "Special",           MenuAction::None,       true,
      MenuRowKind::Scroller, MatchSetupKnob::Special     },
    { "Procedural",        MenuAction::None,       true,
      MenuRowKind::Scroller, MatchSetupKnob::UseGen      },
    { "Gen seed",          MenuAction::None,       true,
      MenuRowKind::Scroller, MatchSetupKnob::GenSeed     },
    { "Gen size",          MenuAction::None,       true,
      MenuRowKind::Scroller, MatchSetupKnob::GenLevel    },
    { "Gen density",       MenuAction::None,       true,
      MenuRowKind::Scroller, MatchSetupKnob::GenDensity  },
    { "Gen perimeter",     MenuAction::None,       true,
      MenuRowKind::Scroller, MatchSetupKnob::GenPerim    },
    { "Repair tiles",      MenuAction::None,       true,
      MenuRowKind::Scroller, MatchSetupKnob::RepairTiles },
    { "Start match",       MenuAction::StartMatch, true  },
    { "Back",              MenuAction::BackToMain, true  },
};

// Preset seed list — picked to be visibly distinct under playtest
// while keeping the cycle short. CLI's `--gen-seed=N` can land any
// uint32; the menu's domain is intentionally coarser.
constexpr std::uint32_t kSeedPresets[] = {
    0u, 1u, 2u, 42u, 1234u, 0xCAFE'F00Du, 0xFEEDu, 0xDEAD'BEEFu,
};
constexpr std::size_t kSeedPresetCount =
    sizeof(kSeedPresets) / sizeof(kSeedPresets[0]);

// Find the preset index for a given seed; returns kSeedPresetCount
// when no preset matches (then the cycler aliases to index 0 on the
// next advance — the CLI's `--gen-seed=N` writes the raw value through,
// the menu cycle is allowed to round to the nearest preset).
std::size_t seedPresetIndex_(std::uint32_t seed) noexcept {
    for (std::size_t i = 0; i < kSeedPresetCount; ++i) {
        if (kSeedPresets[i] == seed) return i;
    }
    return kSeedPresetCount;
}

bool screenHasRows_(UIScreen s) noexcept {
    return s == UIScreen::MainMenu ||
           s == UIScreen::Credits  ||
           s == UIScreen::MatchSetup ||
           s == UIScreen::Pause;
}

const char* matchModeLabel_(MatchMode m) noexcept {
    switch (m) {
        case MatchMode::Deathmatch:       return "Deathmatch";
        case MatchMode::LastShipStanding: return "Last Ship Standing";
    }
    return "?";
}

const char* specialKindLabel_(SpecialKind k) noexcept {
    switch (k) {
        case SpecialKind::Spread:  return "Spread";
        case SpecialKind::Rapid:   return "Rapid";
        case SpecialKind::Sniper:  return "Sniper";
        case SpecialKind::Quintet: return "Quintet";
        case SpecialKind::Heavy:   return "Heavy";
        case SpecialKind::Quad:    return "Quad";
        case SpecialKind::Shotgun: return "Shotgun";
        case SpecialKind::Mine:    return "Mine";
        case SpecialKind::Bouncer: return "Bouncer";
        case SpecialKind::Homer:   return "Homer";
    }
    return "?";
}

} // namespace

UISystem::UISystem(threadmaxx::Engine* engine, UIScreen initial) noexcept
    : engine_(engine), current_(initial) {
    if (screenHasRows_(current_)) resetFocusToFirstEnabled_();
}

bool UISystem::setCurrent(UIScreen newScreen) noexcept {
    if (newScreen == current_) return false;
    const UIScreen from = current_;
    current_ = newScreen;
    if (screenHasRows_(current_)) {
        resetFocusToFirstEnabled_();
    } else {
        focusIndex_ = -1;
    }
    if (engine_) {
        engine_->events<UIScreenChanged>().emit(UIScreenChanged{from, newScreen, {}});
    }
    return true;
}

void UISystem::update(threadmaxx::SystemContext& ctx) {
    // The state machine is driven by the host (main.cpp) which polls
    // GLFW for UI key edges each frame. update() stays a no-op so the
    // wave scheduler sees an empty system — this is what keeps
    // determinism unaffected for CLI-direct-jump runs (where
    // current_==None and we'd never paint a menu anyway).
    (void)ctx;
}

std::span<const MenuRow> UISystem::rows(UIScreen screen) const noexcept {
    switch (screen) {
        case UIScreen::MainMenu:   return { kMainMenuRows,   std::size(kMainMenuRows)   };
        case UIScreen::Credits:    return { kCreditsRows,    std::size(kCreditsRows)    };
        case UIScreen::MatchSetup: return { kMatchSetupRows, std::size(kMatchSetupRows) };
        case UIScreen::Pause:      return { kPauseRows,      std::size(kPauseRows)      };
        default:                   return {};
    }
}

void UISystem::resetFocusToFirstEnabled_() noexcept {
    const auto rs = rows(current_);
    for (std::size_t i = 0; i < rs.size(); ++i) {
        if (rs[i].enabled) {
            focusIndex_ = static_cast<std::int32_t>(i);
            return;
        }
    }
    focusIndex_ = -1;
}

void UISystem::moveFocus(std::int32_t delta) noexcept {
    const auto rs = rows(current_);
    if (rs.empty() || delta == 0) return;
    if (focusIndex_ < 0) {
        resetFocusToFirstEnabled_();
        return;
    }
    const std::int32_t n     = static_cast<std::int32_t>(rs.size());
    const std::int32_t dir   = delta > 0 ? 1 : -1;
    const std::int32_t steps = delta > 0 ? delta : -delta;

    // Apply `steps` single-row advances. Each advance walks until it
    // lands on an enabled row (skipping disabled rows along the way);
    // if a full loop produces no enabled row the focus stays put. The
    // outer `steps` loop lets `moveFocus(±N)` jump N rows in one call.
    for (std::int32_t s = 0; s < steps; ++s) {
        std::int32_t cur     = focusIndex_;
        bool         settled = false;
        for (std::int32_t k = 0; k < n; ++k) {
            cur += dir;
            if (cur < 0)  cur = n - 1;
            if (cur >= n) cur = 0;
            if (rs[static_cast<std::size_t>(cur)].enabled) {
                focusIndex_ = cur;
                settled = true;
                break;
            }
        }
        if (!settled) return;  // no enabled rows reachable; bail
    }
}

MenuAction UISystem::acceptFocused() noexcept {
    const auto rs = rows(current_);
    if (focusIndex_ < 0 || focusIndex_ >= static_cast<std::int32_t>(rs.size())) {
        return MenuAction::None;
    }
    const MenuRow& row = rs[static_cast<std::size_t>(focusIndex_)];
    if (!row.enabled) return MenuAction::None;

    switch (row.action) {
        case MenuAction::SingleMatch:
            // 2026-05-29 — also set `pendingStartMatch_` so the host
            // applies `matchSetup_` via the same engine-restart path
            // StartMatch uses. Without this, "Single Match" picked from
            // the MainMenu silently kept whatever pre-restart world was
            // initialised at launch (1H+3B synthetic arena for no-CLI
            // launches), ignoring any settings the user had since cycled
            // via `Level Setup` -> Back. matchSetup_ always reflects the
            // most recently edited values, so applying it here is the
            // correct posture for both "just hit Single Match" and "edit
            // then back-out to MainMenu then Single Match".
            pendingStartMatch_ = true;
            setCurrent(UIScreen::None);
            break;
        case MenuAction::Credits:
            setCurrent(UIScreen::Credits);
            break;
        case MenuAction::LevelSetup:
            setCurrent(UIScreen::MatchSetup);
            break;
        case MenuAction::BackToMain:
            setCurrent(UIScreen::MainMenu);
            break;
        case MenuAction::StartMatch:
            // M6.2 — host observes the sticky flag on the next frame,
            // applies `matchSetup_` (via TouGame::setMatchSetup +
            // engine restart, the latter landing in M6.4 alongside
            // "Restart match"), then clears the flag. UISystem
            // additionally dismisses the menu so the host's
            // engine.paused() bind unfreezes the simulation cleanly
            // even before the restart machinery is in place.
            pendingStartMatch_ = true;
            setCurrent(UIScreen::None);
            break;
        case MenuAction::Resume:
            // M6.4 — dismiss the Pause menu. Host's engine.paused()
            // bind reads !menuActive() on the next frame and unfreezes
            // the sim. Replay record/play stay in sync because the
            // paused frames they skipped emitting / advancing on are
            // bit-identical no-ops in the recorded stream.
            setCurrent(UIScreen::None);
            break;
        case MenuAction::RestartMatch:
            // M6.4 — sticky flag the host drains, same posture as M6.2
            // StartMatch. Engine-restart-with-MatchSetup wiring is the
            // remaining piece (deferred to a focused follow-up, like
            // M6.2's deferred apply path). Dismissing the menu unblocks
            // the sim so the host's restart routine runs against an
            // unpaused engine if it wants to.
            pendingRestartMatch_ = true;
            setCurrent(UIScreen::None);
            break;
        case MenuAction::ReturnToMainMenu:
            // M6.4 — sticky flag for any host-side cleanup (close
            // replay file, reset stats, etc.) AND transition to
            // MainMenu so the menuActive() bind keeps the sim paused
            // behind the main menu. The combined behaviour matches the
            // M6.1 "no CLI args" launch posture (engine paused, MainMenu
            // up).
            pendingReturnToMainMenu_ = true;
            setCurrent(UIScreen::MainMenu);
            break;
        case MenuAction::Quit:
            pendingQuit_ = true;
            break;
        case MenuAction::Options:
            std::fprintf(stderr, "[tou2d] Options screen not implemented (M6.5)\n");
            break;
        case MenuAction::Benchmark:
            std::fprintf(stderr, "[tou2d] Benchmark preset not implemented (M6.5)\n");
            break;
        case MenuAction::Continue:
        case MenuAction::None:
            break;
    }
    return row.action;
}

void UISystem::cycleFocused(std::int32_t delta) noexcept {
    if (delta == 0) return;
    const auto rs = rows(current_);
    if (focusIndex_ < 0 ||
        focusIndex_ >= static_cast<std::int32_t>(rs.size())) {
        return;
    }
    const MenuRow& row = rs[static_cast<std::size_t>(focusIndex_)];
    if (row.kind != MenuRowKind::Scroller || !row.enabled) return;
    cycleKnob_(row.scrollerKnob, delta);
}

namespace {

// Wrap `value + delta` into `[0, modulus)`. `delta` may be any signed
// magnitude; the result is always a valid index. Equivalent to
// Python's `(value + delta) % modulus` but defined for negative deltas.
std::int64_t wrapInRange_(std::int64_t value,
                          std::int64_t delta,
                          std::int64_t modulus) noexcept {
    if (modulus <= 0) return 0;
    std::int64_t r = (value + delta) % modulus;
    if (r < 0) r += modulus;
    return r;
}

} // namespace

void UISystem::cycleKnob_(MatchSetupKnob knob, std::int32_t delta) noexcept {
    switch (knob) {
        case MatchSetupKnob::Humans: {
            // [1, kMaxHumans] — at least one human player.
            const std::int64_t lo = 1;
            const std::int64_t hi = static_cast<std::int64_t>(kMaxHumans);
            const std::int64_t span = hi - lo + 1;
            matchSetup_.numHumans = static_cast<std::uint8_t>(
                lo + wrapInRange_(matchSetup_.numHumans - lo, delta, span));
            break;
        }
        case MatchSetupKnob::Bots: {
            // [0, kMaxBots] — must keep numHumans + numBots >= 2 at the
            // host level on Start; the cycler itself doesn't enforce
            // (the host already rejects 1+0 today).
            const std::int64_t span =
                static_cast<std::int64_t>(kMaxBots) + 1;
            matchSetup_.numBots = static_cast<std::uint8_t>(
                wrapInRange_(matchSetup_.numBots, delta, span));
            break;
        }
        case MatchSetupKnob::Mode: {
            // Two values; any nonzero delta toggles.
            const std::int64_t cur =
                (matchSetup_.matchMode == MatchMode::Deathmatch) ? 0 : 1;
            matchSetup_.matchMode =
                (wrapInRange_(cur, delta, 2) == 0)
                    ? MatchMode::Deathmatch
                    : MatchMode::LastShipStanding;
            break;
        }
        case MatchSetupKnob::Special: {
            const std::int64_t span = kSpecialKindCount;
            matchSetup_.specialKind = static_cast<SpecialKind>(
                wrapInRange_(static_cast<std::int64_t>(matchSetup_.specialKind),
                             delta, span));
            break;
        }
        case MatchSetupKnob::UseGen: {
            const std::int64_t cur = matchSetup_.useGen ? 1 : 0;
            matchSetup_.useGen = (wrapInRange_(cur, delta, 2) != 0);
            break;
        }
        case MatchSetupKnob::GenSeed: {
            // Cycle through `kSeedPresets`. If the current seed isn't a
            // preset (CLI override), the next cycle snaps to index 0
            // — explicit + reversible.
            std::size_t idx = seedPresetIndex_(matchSetup_.genCfg.seed);
            if (idx >= kSeedPresetCount) idx = 0;
            else idx = static_cast<std::size_t>(
                wrapInRange_(static_cast<std::int64_t>(idx), delta,
                             static_cast<std::int64_t>(kSeedPresetCount)));
            matchSetup_.genCfg.seed = kSeedPresets[idx];
            break;
        }
        case MatchSetupKnob::GenLevel: {
            // [0, 4] — ggLevel domain pinned by `kGgLevelCells`.
            const std::int64_t span = 5;
            matchSetup_.genCfg.ggLevel = static_cast<std::uint8_t>(
                wrapInRange_(matchSetup_.genCfg.ggLevel, delta, span));
            break;
        }
        case MatchSetupKnob::GenDensity: {
            // [0, 100] in steps of 10 — 11 buckets.
            constexpr std::int64_t kStep = 10;
            constexpr std::int64_t kSpan = 11;
            const std::int64_t cur =
                static_cast<std::int64_t>(matchSetup_.genCfg.stuffDensity) /
                kStep;
            matchSetup_.genCfg.stuffDensity = static_cast<std::uint8_t>(
                wrapInRange_(cur, delta, kSpan) * kStep);
            break;
        }
        case MatchSetupKnob::GenPerim: {
            const std::int64_t cur =
                matchSetup_.genCfg.perimeterBedrock ? 1 : 0;
            matchSetup_.genCfg.perimeterBedrock =
                static_cast<std::uint8_t>(wrapInRange_(cur, delta, 2));
            break;
        }
        case MatchSetupKnob::RepairTiles: {
            // [0, 32] in steps of 4 — 9 buckets.
            constexpr std::int64_t kStep = 4;
            constexpr std::int64_t kSpan = 9;
            const std::int64_t cur =
                static_cast<std::int64_t>(matchSetup_.genCfg.repairTileCount) /
                kStep;
            matchSetup_.genCfg.repairTileCount = static_cast<std::uint8_t>(
                wrapInRange_(cur, delta, kSpan) * kStep);
            break;
        }
        case MatchSetupKnob::Count:
            break;
    }
}

std::size_t UISystem::formatRow(std::int32_t rowIdx,
                                char*        buf,
                                std::size_t  bufN) const noexcept {
    if (bufN == 0) return 0;
    buf[0] = '\0';
    const auto rs = rows(current_);
    if (rowIdx < 0 || rowIdx >= static_cast<std::int32_t>(rs.size())) {
        return 0;
    }
    const MenuRow& row = rs[static_cast<std::size_t>(rowIdx)];
    if (row.kind == MenuRowKind::Action) {
        const std::size_t n = std::strlen(row.label);
        const std::size_t copy = (n < bufN - 1) ? n : bufN - 1;
        std::memcpy(buf, row.label, copy);
        buf[copy] = '\0';
        return copy;
    }
    // Scroller — "<label>: <value>".
    char valBuf[40];
    formatKnobValue_(row.scrollerKnob, valBuf, sizeof(valBuf));
    const int written =
        std::snprintf(buf, bufN, "%s: %s", row.label, valBuf);
    if (written < 0) return 0;
    const std::size_t w = static_cast<std::size_t>(written);
    return (w < bufN) ? w : bufN - 1;
}

std::size_t UISystem::formatKnobValue_(MatchSetupKnob knob,
                                       char*          buf,
                                       std::size_t    bufN) const noexcept {
    if (bufN == 0) return 0;
    int n = 0;
    switch (knob) {
        case MatchSetupKnob::Humans:
            n = std::snprintf(buf, bufN, "%u",
                              unsigned(matchSetup_.numHumans));
            break;
        case MatchSetupKnob::Bots:
            n = std::snprintf(buf, bufN, "%u",
                              unsigned(matchSetup_.numBots));
            break;
        case MatchSetupKnob::Mode:
            n = std::snprintf(buf, bufN, "%s",
                              matchModeLabel_(matchSetup_.matchMode));
            break;
        case MatchSetupKnob::Special:
            n = std::snprintf(buf, bufN, "%s",
                              specialKindLabel_(matchSetup_.specialKind));
            break;
        case MatchSetupKnob::UseGen:
            n = std::snprintf(buf, bufN, "%s",
                              matchSetup_.useGen ? "On" : "Off");
            break;
        case MatchSetupKnob::GenSeed:
            n = std::snprintf(buf, bufN, "0x%08x",
                              matchSetup_.genCfg.seed);
            break;
        case MatchSetupKnob::GenLevel:
            n = std::snprintf(buf, bufN, "%u",
                              unsigned(matchSetup_.genCfg.ggLevel));
            break;
        case MatchSetupKnob::GenDensity:
            n = std::snprintf(buf, bufN, "%u%%",
                              unsigned(matchSetup_.genCfg.stuffDensity));
            break;
        case MatchSetupKnob::GenPerim:
            n = std::snprintf(buf, bufN, "%s",
                              matchSetup_.genCfg.perimeterBedrock ? "On" : "Off");
            break;
        case MatchSetupKnob::RepairTiles:
            n = std::snprintf(buf, bufN, "%u",
                              unsigned(matchSetup_.genCfg.repairTileCount));
            break;
        case MatchSetupKnob::Count:
            buf[0] = '\0';
            break;
    }
    if (n < 0) return 0;
    return static_cast<std::size_t>(n) < bufN ? static_cast<std::size_t>(n)
                                              : bufN - 1;
}

} // namespace tou2d
