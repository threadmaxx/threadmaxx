/// @file panels/MenuBar.cpp

#include <threadmaxx_studio/panels/menu_bar.hpp>

#include <threadmaxx_editor/backend.hpp>

namespace threadmaxx::studio {

void MenuBar::render(editor::IEditorBackend& backend, IStudioDataSource&) {
    // ST3 emits one drawText per registered action so the chrome row
    // is visible and snapshot-asserts are possible. Real backends will
    // turn this into a proper menu strip in a later batch.
    float x = 0.0f;
    for (const auto& a : actions_) {
        const std::string label = a.menu + " > " + a.label;
        backend.drawText(label, x, 0.0f);
        x += 100.0f;
    }
}

void MenuBar::addAction(std::string_view menu, std::string_view label,
                        std::function<void()> callback) {
    for (auto& a : actions_) {
        if (a.menu == menu && a.label == label) {
            a.callback = std::move(callback);
            return;
        }
    }
    actions_.push_back(Action{std::string(menu), std::string(label),
                              std::move(callback)});
}

bool MenuBar::trigger(std::string_view menu, std::string_view label) {
    for (auto& a : actions_) {
        if (a.menu == menu && a.label == label) {
            if (a.callback) {
                a.callback();
            }
            return true;
        }
    }
    return false;
}

} // namespace threadmaxx::studio
