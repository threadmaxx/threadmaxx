#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

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
    None         = 0,
    Continue     = 1,   ///< Resume an in-flight match (greyed in v1).
    SingleMatch  = 2,   ///< Start a default 1H+3B match (dismisses menu).
    LevelSetup   = 3,   ///< M6.2 stub.
    Options      = 4,   ///< M6.5 stub.
    Benchmark    = 5,   ///< M6.5 stress-preset stub.
    Credits      = 6,   ///< Transition to UIScreen::Credits.
    Quit         = 7,   ///< Set pendingQuit() — host exits frame loop.
    BackToMain   = 8,   ///< Pop back to MainMenu (used by Credits screen).
};

/// M6.1 — single menu row descriptor. `enabled == false` paints greyed
/// and is skipped by `moveFocus`. The `label` pointer is borrowed; v1
/// rows are constexpr string literals owned by UISystem itself.
struct MenuRow {
    const char* label;
    MenuAction  action;
    bool        enabled;
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

private:
    void resetFocusToFirstEnabled_() noexcept;

    threadmaxx::Engine* engine_;
    UIScreen            current_;
    std::int32_t        focusIndex_ = -1;
    bool                pendingQuit_ = false;
};

} // namespace tou2d
