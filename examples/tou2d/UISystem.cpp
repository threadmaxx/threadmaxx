#include "UISystem.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/System.hpp>

#include <cstdio>

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

bool screenHasRows_(UIScreen s) noexcept {
    return s == UIScreen::MainMenu || s == UIScreen::Credits;
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
        case UIScreen::MainMenu: return { kMainMenuRows, std::size(kMainMenuRows) };
        case UIScreen::Credits:  return { kCreditsRows,  std::size(kCreditsRows)  };
        default:                 return {};
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
            // Dismiss menu; host unpauses on the next frame.
            setCurrent(UIScreen::None);
            break;
        case MenuAction::Credits:
            setCurrent(UIScreen::Credits);
            break;
        case MenuAction::BackToMain:
            setCurrent(UIScreen::MainMenu);
            break;
        case MenuAction::Quit:
            pendingQuit_ = true;
            break;
        case MenuAction::LevelSetup:
            std::fprintf(stderr, "[tou2d] Level Setup screen not implemented (M6.2)\n");
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

} // namespace tou2d
