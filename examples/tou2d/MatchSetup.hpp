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

#include <array>
#include <cstdint>

namespace tou2d {

/// M6.3 — per-slot override for slots [0, kMatchSetupSlotCount).
///
/// Sentinels preserve the "menu defaults = CLI auto-cycle" determinism
/// contract: when every field on every slot holds its sentinel, the
/// effective spawn shape is bit-identical to the pre-M6.3 CLI path.
///
///   * `tag`         — 3 ASCII chars (A-Z + space). All-spaces means
///                     "auto" → TouGame fills in a slot-derived label
///                     (P0/P1/P2/P3) at spawn time.
///   * `role`        — 0=Auto (use numHumans/numBots), 1=Human,
///                     2=Bot. Overriding lets non-contiguous patterns
///                     like H/B/H/B; total ship count stays
///                     `numHumans + numBots`.
///   * `shipKindIdx` — 0xFF=Auto (use TouGame's kAtlasSeeds[slot%4]
///                     default). Any other value indexes `kShipKinds`
///                     directly; out-of-range falls back to Basic.
///   * `paletteIdx`  — 0xFF=Auto (use slot%4). 0..3 picks one of the
///                     four canonical sprite atlases (yellow/blue/
///                     red/green) regardless of slot.
struct PlayerSlotSetup {
    std::array<char, 3> tag         = {' ', ' ', ' '};
    std::uint8_t        role        = 0;     ///< 0=Auto, 1=Human, 2=Bot
    std::uint8_t        shipKindIdx = 0xFFu; ///< 0xFF = auto
    std::uint8_t        paletteIdx  = 0xFFu; ///< 0xFF = auto
    /// M7.4 — per-slot faction override. 0xFF (`kFactionAuto`) means
    /// "use the slot index as the faction" — every default-init slot
    /// ends up in its own faction, reproducing the pre-M7.4 free-for-
    /// all. Any other value pins the LocalPlayer.factionId at spawn
    /// time. Two slots sharing the same value are allies.
    std::uint8_t        factionId   = 0xFFu;
    std::uint8_t        _pad[1]     = {};
};
static_assert(sizeof(PlayerSlotSetup) == 8,
              "PlayerSlotSetup must stay 8 bytes — embedded in MatchSetup "
              "as a fixed-size array; layout change must update the UI "
              "scroller table.");

/// M6.3 — number of per-slot overrides exposed by the PlayerSetup
/// screen. Matches `kMaxHumans` (4); the menu lets the user override
/// the first four slots specifically because those are the keyboard-
/// eligible positions. Bots beyond slot 3 cycle automatically per the
/// existing `slot % 4` pattern in TouGame.
inline constexpr std::size_t kMatchSetupSlotCount = 4;

/// 2026-05-31 — sentinel for `MatchSetup::importedLevelIdx` meaning
/// "no level picked" / synthetic-arena fallback. Indices `[0, N)` map
/// into the host's enumerated `assets/levels/*` list (see
/// `LevelEnumerator.hpp`); `kImportedLevelNone` is a permanent reserve.
inline constexpr std::uint8_t kImportedLevelNone = 0xFFu;

/// All knobs needed to start a match. Field defaults match the M5.1
/// CLI defaults so a default-constructed `MatchSetup` reproduces the
/// "no CLI args" gameplay shape (1 human + 3 bots, deathmatch, spread
/// weapon, synthetic arena fallback).
///
/// M6.3 — extends with a `playerSlots` array of per-slot overrides.
/// Default-init holds the all-sentinel state so unedited menu runs
/// remain bit-identical to the CLI path.
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
    /// settings. When `false`, the host first tries the imported level
    /// selected by `importedLevelIdx`, then falls back to the synthetic
    /// arena.
    bool                  useGen = false;

    /// 2026-05-31 — menu picks one of the enumerated `assets/levels/*`
    /// imported dirs. `0xFF` (`kImportedLevelNone`) means "no imported
    /// level selected" — synthetic arena fallback. Any other value
    /// indexes the host-supplied enumeration list (sorted by name); the
    /// host resolves index → path before calling `TouGame::setLevelDir`
    /// at match-start time. The CLI's `--level <path>` still wins when
    /// set; this knob is ignored on CLI-direct-jump runs.
    std::uint8_t          importedLevelIdx = 0xFFu;
    std::uint8_t          _pad[2] = {};
    ProceduralLevelConfig genCfg{};

    /// M6.3 — per-slot overrides; default = all-sentinel = CLI auto-cycle.
    std::array<PlayerSlotSetup, kMatchSetupSlotCount> playerSlots{};
};

static_assert(sizeof(MatchSetup) ==
                  16 + kMatchSetupSlotCount * sizeof(PlayerSlotSetup),
              "MatchSetup is engine-runtime state; layout change must "
              "be coordinated with the UI scroller table.");

} // namespace tou2d
