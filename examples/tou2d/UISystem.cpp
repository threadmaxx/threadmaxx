#include "UISystem.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/System.hpp>

namespace tou2d {

UISystem::UISystem(threadmaxx::Engine* engine, UIScreen initial) noexcept
    : engine_(engine), current_(initial) {}

bool UISystem::setCurrent(UIScreen newScreen) noexcept {
    if (newScreen == current_) return false;
    const UIScreen from = current_;
    current_ = newScreen;
    if (engine_) {
        engine_->events<UIScreenChanged>().emit(UIScreenChanged{from, newScreen, {}});
    }
    return true;
}

void UISystem::update(threadmaxx::SystemContext& ctx) {
    // M6.0b: empty body. The skeleton is in place so M6.1's menu
    // handlers slot in here without touching wave ordering. We
    // explicitly touch the context to silence unused-parameter
    // warnings (the engine sees every update call regardless).
    (void)ctx;
}

} // namespace tou2d
