#pragma once

#include "DemoTypes.hpp"
#include "MatchSetup.hpp"

#include <threadmaxx/System.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace threadmaxx { class Engine; }

namespace tou2d {

/// M6.1 — semantic action a menu row fires when accepted. Maps the
/// concrete UI choice ("Single Match") onto a stable identifier the
/// host can switch on without re-parsing label strings (so M6.5's
/// settings.dat / future localisation never alias). `None` is the
/// no-op return when the focused row is disabled (greyed) or there's
/// no focused row.
enum class MenuAction : std::uint8_t {
    None              = 0,
    Continue          = 1,   ///< Resume an in-flight match (greyed in v1).
    SingleMatch       = 2,   ///< Start a default 1H+3B match (dismisses menu).
    LevelSetup        = 3,   ///< M6.2 — jumps to UIScreen::MatchSetup.
    Options           = 4,   ///< M6.5 stub.
    Benchmark         = 5,   ///< M6.5 stress-preset stub.
    Credits           = 6,   ///< Transition to UIScreen::Credits.
    Quit              = 7,   ///< Set pendingQuit() — host exits frame loop.
    BackToMain        = 8,   ///< Pop back to MainMenu (used by Credits / MatchSetup).
    StartMatch        = 9,   ///< M6.2 — set pendingStartMatch_, dismiss menu.
    Resume            = 10,  ///< M6.4 — close Pause screen, unfreeze sim.
    RestartMatch      = 11,  ///< M6.4 — set pendingRestartMatch_, dismiss menu.
    ReturnToMainMenu  = 12,  ///< M6.4 — set pendingReturnToMainMenu_, jump to MainMenu.
    PlayerSetup       = 13,  ///< M6.3 — jumps to UIScreen::PlayerSetup.
    Rematch           = 14,  ///< M6.6 — set pendingRematch_, dismiss menu (host re-applies setup).
    ReturnToSetup     = 15,  ///< M6.6 — jump back to UIScreen::MatchSetup (carry over matchSetup_).
};

/// M6.2 — distinguishes a plain "fire on accept" row from a horizontal
/// scroller row (left/right cycles a value on `matchSetup()`). The host
/// dispatches `cycleFocused(±1)` on UiLeft/UiRight; scroller rows then
/// mutate their bound knob. Accept on a scroller row falls through to
/// `action` (typically `MenuAction::None` so accept doesn't double-fire).
enum class MenuRowKind : std::uint8_t {
    Action   = 0,
    Scroller = 1,
    /// M6.6 — read-only formatted row. `formatRow` consults the row's
    /// `slotIdx` to render dynamic content from the active screen's
    /// backing state (currently: the captured `MatchResults` for the
    /// Results screen). Display rows MUST have `enabled = false` so
    /// `moveFocus` skips them; `acceptFocused` ignores them anyway.
    Display  = 2,
};

/// M6.6 — per-slot scoreboard row captured at round-end. Stable layout
/// (engine-runtime state only; not persisted across sessions). Fields:
///   * `tag`         — 3 ASCII chars resolved from `MatchSetup::playerSlots`
///                     when the user set one; else a slot-derived "P0".."P3"
///                     auto label.
///   * `active`      — 1 if this slot held a live ship at any point in the
///                     match; 0 if it was empty (slot > numHumans+numBots).
///   * `kills`       — `Ship::kills` at round-end (frag count).
///   * `isBot`       — 1 if the slot was AI-driven, 0 if a local human.
///   * `shipKindIdx` — index into `kShipKinds` for ship-name lookup.
struct MatchResultsSlot {
    std::array<char, 3> tag         = {' ', ' ', ' '};
    std::uint8_t        active      = 0;
    std::uint16_t       kills       = 0;
    std::uint8_t        isBot       = 0;
    std::uint8_t        shipKindIdx = 0;
};
static_assert(sizeof(MatchResultsSlot) == 8,
              "MatchResultsSlot stays 8 bytes — embedded in MatchResults as a "
              "fixed-size array; layout change must update the Results "
              "row formatter.");

/// M6.6 — snapshot of a finished match. Captured by the host on the
/// rising edge of `RoundEnded`; handed to `UISystem::showResults` which
/// stores it for `formatRow` to render. Only the first
/// `kMatchSetupSlotCount` slots are recorded — sufficient for the v1
/// 4-row scoreboard. Larger bot rosters (kMaxPlayerSlots = 67) are
/// summarised by the winner banner only.
struct MatchResults {
    std::uint8_t  winnerSlot  = 0;
    std::uint8_t  _pad        = 0;
    std::uint16_t winnerKills = 0;
    std::uint8_t  _pad2[4]    = {};
    std::array<MatchResultsSlot, kMatchSetupSlotCount> slots{};
};
static_assert(sizeof(MatchResults) == 8 + kMatchSetupSlotCount * 8,
              "MatchResults layout must match the Results-screen row "
              "formatter's expected slot count.");

/// M6.2 — scroller knob identifier. One per editable field on the
/// MatchSetup screen. Order matches `kMatchSetupRows` so a row's
/// `scrollerKnob` field doubles as its row index when needed.
/// Adding a knob is one new enum value + one new row entry + one new
/// switch arm in `cycleKnob_` / `formatKnobValue_`.
enum class MatchSetupKnob : std::uint8_t {
    Humans       = 0,
    Bots         = 1,
    Mode         = 2,
    Special      = 3,
    UseGen       = 4,
    GenSeed      = 5,
    GenLevel     = 6,
    GenDensity   = 7,
    GenPerim     = 8,
    RepairTiles  = 9,
    /// M6.3 — per-slot knobs. The MenuRow's `slotIdx` selects which
    /// slot's PlayerSlotSetup field the cycler reads/writes. Order
    /// inside a slot matches the on-screen vertical order.
    SlotTagChar0 = 10,
    SlotTagChar1 = 11,
    SlotTagChar2 = 12,
    SlotRole     = 13,
    SlotShip     = 14,
    SlotPalette  = 15,
    Count        = 16,  ///< Sentinel (== number of scroller knob classes).
};

/// M6.1 — single menu row descriptor. `enabled == false` paints greyed
/// and is skipped by `moveFocus`. The `label` pointer is borrowed; v1
/// rows are constexpr string literals owned by UISystem itself.
///
/// M6.2 — `kind` distinguishes Action (accept fires `action`) from
/// Scroller (left/right cycles the bound `scrollerKnob`; accept is a
/// no-op via `action == None`). For Action rows `scrollerKnob` is
/// ignored.
struct MenuRow {
    const char*     label;
    MenuAction      action;
    bool            enabled;
    MenuRowKind     kind         = MenuRowKind::Action;
    MatchSetupKnob  scrollerKnob = MatchSetupKnob::Count;
    /// M6.3 — slot index for per-slot knobs (SlotTagChar* / SlotRole /
    /// SlotShip / SlotPalette). Ignored for the global knobs and Action
    /// rows; must be in [0, kMatchSetupSlotCount) for per-slot rows.
    std::uint8_t    slotIdx      = 0;
};

/// M6.0b — top-level UI state machine. Owns `UIScreen current`; the
/// rest of M6 (match setup, pause, results, etc.) lands as additional
/// per-screen rows / handlers. v1 scope (M6.1) wires only the
/// MainMenu and a placeholder Credits screen — the four "stub" rows
/// (LevelSetup / Options / Benchmark / PlayerSetup) log to stderr and
/// stay on MainMenu until later milestones replace them.
///
/// Wave ordering: registered BEFORE `InputSystem`. Today the input
/// "swallow" is implicit — the host pauses the engine whenever
/// `menuActive()` flips on, so `InputSystem::preStep` doesn't run while
/// a menu is up; ships freeze in-place; PlayerInput stays at zero.
///
/// Transition contract: `setCurrent(newScreen)` is the only allowed
/// mutation. It emits a `UIScreenChanged{from, to}` on the engine's
/// typed event channel (when the engine pointer is non-null) and
/// resets `focusIndex_` to the first enabled row of the new screen.
/// Same-screen calls are no-ops (no event, focus unchanged).
class UISystem : public threadmaxx::ISystem {
public:
    explicit UISystem(threadmaxx::Engine* engine = nullptr,
                      UIScreen            initial = UIScreen::None) noexcept;

    const char*              name()   const noexcept override { return "tou2d.ui"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        // No component access yet — the state machine is purely
        // example-side state. M6.5+ may need to read PlayerInput
        // to consume nav keys; that'd add UserData here.
        return threadmaxx::ComponentSet{};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{};
    }

    void update(threadmaxx::SystemContext& ctx) override;

    /// Current screen. Mutated only via `setCurrent`.
    UIScreen current() const noexcept { return current_; }

    /// Transition to a new screen. No-op if `newScreen == current()`.
    /// Emits `UIScreenChanged{from, to}` via the engine's typed event
    /// channel when the engine pointer is non-null. Resets focus to
    /// the first enabled row of the new screen. Returns true if a
    /// transition fired.
    bool setCurrent(UIScreen newScreen) noexcept;

    /// True iff the active screen is a menu (i.e. anything other than
    /// `None`). Renderer-side compositors use this to decide whether
    /// to draw the menu layer this tick; the host uses it to decide
    /// whether the engine should be paused.
    bool menuActive() const noexcept { return current_ != UIScreen::None; }

    /// ---- M6.1 menu model ----------------------------------------------

    /// Rows for an arbitrary screen. Returns empty span for screens
    /// without a row table (e.g. UIScreen::None).
    std::span<const MenuRow> rows(UIScreen screen) const noexcept;

    /// Rows for the currently active screen. Convenience.
    std::span<const MenuRow> currentRows() const noexcept { return rows(current_); }

    /// Index into `currentRows()`. -1 when no row is focused (empty
    /// screen). Never indexes into a disabled row — moveFocus and
    /// setCurrent both skip disabled.
    std::int32_t focusIndex() const noexcept { return focusIndex_; }

    /// Move focus by `delta` enabled rows. Sign picks direction;
    /// `|delta|` is the number of one-row advances applied (each
    /// advance skips disabled rows and wraps at both ends). No-op
    /// when the current screen has no enabled rows. Caller-side
    /// input handlers map UiUp -> moveFocus(-1) and UiDown ->
    /// moveFocus(+1); multi-row helpers (PgUp/PgDn, future) can pass
    /// larger magnitudes without losing the skip-disabled invariant.
    void moveFocus(std::int32_t delta) noexcept;

    /// Fire the focused row's `action`. Returns `MenuAction::None`
    /// when there's no focused row OR the focused row is disabled.
    /// Otherwise dispatches the action's side-effect inside UISystem
    /// (screen transitions, pendingQuit_ flip, stderr stub logs) AND
    /// returns the same action so the host can react if needed.
    MenuAction acceptFocused() noexcept;

    /// True after a `MenuAction::Quit` accept fired. Cleared by the
    /// host (typically after breaking the frame loop). Sticky so a
    /// late frame poll picks it up.
    bool pendingQuit() const noexcept { return pendingQuit_; }
    void clearPendingQuit() noexcept { pendingQuit_ = false; }

    /// ---- M6.2 MatchSetup screen --------------------------------------

    /// Working `MatchSetup` the MatchSetup screen edits in-place.
    /// Defaults match the CLI defaults so an unedited menu run produces
    /// the same gameplay as no-CLI-args today. The host reads this on
    /// `pendingStartMatch()` to know what to apply.
    MatchSetup&       matchSetup() noexcept       { return matchSetup_; }
    const MatchSetup& matchSetup() const noexcept { return matchSetup_; }

    /// Cycle the focused row's bound scroller knob by `delta` (typically
    /// +1 or -1; magnitudes step that many wraps). No-op when:
    ///   * the focused row is `MenuRowKind::Action`, OR
    ///   * there's no focused row, OR
    ///   * `delta == 0`.
    /// Wraps at both ends of each knob's domain so the UI never lands
    /// on a no-op state.
    void cycleFocused(std::int32_t delta) noexcept;

    /// Render-time format for a row at index `rowIdx` into `currentRows()`.
    /// For Action rows writes the static label verbatim. For Scroller
    /// rows writes "<label>: <value>" with the value resolved from
    /// `matchSetup_`. Returns the number of chars written (excluding
    /// the trailing NUL). `bufN` must be >= 1; output is always
    /// NUL-terminated.
    std::size_t formatRow(std::int32_t rowIdx,
                          char*        buf,
                          std::size_t  bufN) const noexcept;

    /// True after a `MenuAction::StartMatch` accept fired. Sticky until
    /// the host calls `clearPendingStartMatch()`. The host should apply
    /// `matchSetup()` to the game world (typically via
    /// `TouGame::setMatchSetup` + an engine restart) before clearing
    /// the flag.
    bool pendingStartMatch() const noexcept { return pendingStartMatch_; }
    void clearPendingStartMatch() noexcept   { pendingStartMatch_ = false; }

    /// ---- M6.6 Results screen -----------------------------------------

    /// Capture a finished match's per-slot scoreboard + winner banner
    /// and flip the screen to `UIScreen::Results`. Idempotent —
    /// successive calls overwrite the captured snapshot (only the last
    /// round's results are kept). The host invokes this from the
    /// rising-edge of `roundEndedFlag()` after collecting stats from
    /// `TouGame::collectMatchResults`.
    void showResults(const MatchResults& r) noexcept;

    /// Read-back of the last `showResults` snapshot. Used by
    /// `formatRow` to render the Display rows, and by the host's
    /// Rematch / ReturnToSetup drains to reseed the next match. Stays
    /// valid across screen transitions (e.g. while the user briefly
    /// navigates to MainMenu and back) — only `showResults` overwrites.
    const MatchResults& matchResults() const noexcept { return matchResults_; }

    /// True after a `MenuAction::Rematch` accept fired. Sticky until
    /// the host calls `clearPendingRematch()`. Same posture as
    /// `pendingRestartMatch` — the host re-applies `matchSetup()` and
    /// restarts the engine; the matchSetup_ contents are unchanged so
    /// the new match has the same humans/bots/mode/slots as the one
    /// that just ended.
    bool pendingRematch() const noexcept { return pendingRematch_; }
    void clearPendingRematch() noexcept   { pendingRematch_ = false; }

    /// ---- M6.4 Pause screen --------------------------------------------

    /// True after a `MenuAction::RestartMatch` accept fired on the Pause
    /// screen. Sticky until cleared. The host responds by rebuilding the
    /// match world using `matchSetup()` (typically the same engine-restart
    /// path StartMatch will use once wired). The menu is dismissed
    /// (`setCurrent(UIScreen::None)`) at the accept site so the host's
    /// `engine.paused()` bind cleanly unfreezes the sim even before any
    /// restart code runs.
    bool pendingRestartMatch() const noexcept { return pendingRestartMatch_; }
    void clearPendingRestartMatch() noexcept   { pendingRestartMatch_ = false; }

    /// True after a `MenuAction::ReturnToMainMenu` accept fired. Sticky
    /// until cleared. The host typically drops any in-flight match state
    /// (or — in the M6.4 first-cut — just leaves the sim paused on the
    /// current world). The UI is transitioned to `UIScreen::MainMenu`
    /// at the accept site so the sim stays paused via the `menuActive`
    /// bind.
    bool pendingReturnToMainMenu() const noexcept { return pendingReturnToMainMenu_; }
    void clearPendingReturnToMainMenu() noexcept   { pendingReturnToMainMenu_ = false; }

private:
    void resetFocusToFirstEnabled_() noexcept;
    /// M6.3 — `slotIdx` is meaningful only for per-slot knobs
    /// (SlotTagChar* / SlotRole / SlotShip / SlotPalette); ignored for
    /// the global knobs. Caller pre-clamps to [0, kMatchSetupSlotCount).
    void cycleKnob_(MatchSetupKnob knob,
                    std::uint8_t   slotIdx,
                    std::int32_t   delta) noexcept;
    std::size_t formatKnobValue_(MatchSetupKnob knob,
                                 std::uint8_t   slotIdx,
                                 char*          buf,
                                 std::size_t    bufN) const noexcept;

    threadmaxx::Engine* engine_;
    UIScreen            current_;
    std::int32_t        focusIndex_              = -1;
    bool                pendingQuit_             = false;
    bool                pendingStartMatch_       = false;
    bool                pendingRestartMatch_     = false;
    bool                pendingReturnToMainMenu_ = false;
    bool                pendingRematch_          = false;
    MatchSetup          matchSetup_{};
    MatchResults        matchResults_{};
};

} // namespace tou2d
