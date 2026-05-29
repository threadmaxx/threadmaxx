#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

namespace threadmaxx { class Engine; }

namespace tou2d {

/// M6.0b — top-level UI state machine. Owns `UIScreen current`; the
/// rest of M6 (main menu, match setup, pause, results, etc.) lands as
/// per-screen handlers inside `update`. v1 scope keeps `update` a
/// no-op when `current == None` so legacy CLI-direct-jump callers and
/// headless smoke runs continue to bypass the menu entirely (a UI
/// system registered but parked on `None` is zero-cost).
///
/// Wave ordering: register BEFORE `InputSystem`. When `current` is a
/// non-`None` menu screen, future versions of this update will SWALLOW
/// gameplay input so the player ship doesn't thrust while a menu is
/// up. Today the swallow path isn't wired (no menu content yet) — the
/// ordering is captured for M6.1.
///
/// Transition contract: `setCurrent(newScreen)` is the only allowed
/// mutation. It emits a `UIScreenChanged{from, to}` on the engine's
/// typed event channel; same-screen calls are no-ops (no event). The
/// engine pointer is borrowed and may be null in tests — in that case
/// the transition fires without an event emit (state still updates).
class UISystem : public threadmaxx::ISystem {
public:
    explicit UISystem(threadmaxx::Engine* engine = nullptr,
                      UIScreen            initial = UIScreen::None) noexcept;

    const char*              name()   const noexcept override { return "tou2d.ui"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        // No component access yet — the state machine is purely
        // example-side state. M6.1+ may need to read PlayerInput
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
    /// channel when the engine pointer is non-null. Returns true if a
    /// transition fired (i.e. screens differed).
    bool setCurrent(UIScreen newScreen) noexcept;

    /// True iff the active screen is a menu (i.e. anything other than
    /// `None`). Renderer-side compositors use this to decide whether
    /// to draw the menu layer this tick.
    bool menuActive() const noexcept { return current_ != UIScreen::None; }

private:
    threadmaxx::Engine* engine_;
    UIScreen            current_;
};

} // namespace tou2d
