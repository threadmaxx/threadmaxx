#pragma once

// M6.2 — match configuration captured as a single POD.
//
// `MatchSetup` is the union of every gameplay knob the CLI exposes
// today (humans, bots, mode, special weapon, generator config). Both
// the CLI parse path (`main.cpp`) and the future menu setup screen
// (`UISystem`) populate the same POD; a single setter on `TouGame`
// (`setMatchSetup`) fans the fields out to the existing per-system
// setters. That guarantees the menu path is determinism-equivalent
// to the CLI path: same `MatchSetup` → same `commitHash` stream.

#include "DemoTypes.hpp"
#include "ProceduralLevel.hpp"

#include <cstdint>

namespace tou2d {

/// All knobs needed to start a match. Field defaults match the M5.1
/// CLI defaults so a default-constructed `MatchSetup` reproduces the
/// "no CLI args" gameplay shape (1 human + 3 bots, deathmatch, spread
/// weapon, synthetic arena fallback).
///
/// Bit layout is **not** stable across versions — this struct is
/// engine-runtime state, not on-disk format. The replay header has
/// its own pinned layout (`ReplayHeader` in `Replay.hpp`).
struct MatchSetup {
    std::uint8_t  numHumans   = 1;
    std::uint8_t  numBots     = 3;
    MatchMode     matchMode   = MatchMode::Deathmatch;
    SpecialKind   specialKind = SpecialKind::Spread;

    /// When `useGen == true`, `genCfg` carries the procedural generator
    /// settings. When `false`, the host falls back to either an imported
    /// `levelDir` (CLI-only path) or the synthetic arena. The menu's
    /// MatchSetup screen toggles `useGen` only — `levelDir` is CLI-only
    /// because the menu has no directory enumerator.
    bool                  useGen = false;
    std::uint8_t          _pad[3] = {};
    ProceduralLevelConfig genCfg{};
};

static_assert(sizeof(MatchSetup) == 16,
              "MatchSetup is engine-runtime state; layout change must "
              "be coordinated with the UI scroller table.");

} // namespace tou2d
