/// @file PanelHost.cpp
/// @brief `PanelHost` impl. Linear scan over a small vector — the cap
/// is `kMaxPanels = 256`, the steady-state visible-panel count for
/// v1.0 is < 50, so a hash map would only add weight.

#include <threadmaxx_studio/config.hpp>
#include <threadmaxx_studio/studio.hpp>

#include <threadmaxx_editor/layout.hpp>

namespace threadmaxx::studio {

bool PanelHost::registerPanel(IStudioPanel* panel) {
    if (panel == nullptr) {
        return false;
    }
    if (slots_.size() >= kMaxPanels) {
        return false;
    }
    const auto id = panel->id();
    for (const auto& slot : slots_) {
        if (slot.panel->id() == id) {
            return false;
        }
    }
    slots_.push_back(Slot{panel, true});
    return true;
}

bool PanelHost::unregisterPanel(std::string_view id) {
    for (auto it = slots_.begin(); it != slots_.end(); ++it) {
        if (it->panel->id() == id) {
            slots_.erase(it);
            return true;
        }
    }
    return false;
}

IStudioPanel* PanelHost::findPanel(std::string_view id) const noexcept {
    for (const auto& slot : slots_) {
        if (slot.panel->id() == id) {
            return slot.panel;
        }
    }
    return nullptr;
}

void PanelHost::setVisible(std::string_view id, bool visible) {
    for (auto& slot : slots_) {
        if (slot.panel->id() == id) {
            slot.visible = visible;
            return;
        }
    }
}

bool PanelHost::isVisible(std::string_view id) const noexcept {
    for (const auto& slot : slots_) {
        if (slot.panel->id() == id) {
            return slot.visible;
        }
    }
    return false;
}

void PanelHost::saveTo(editor::LayoutManager& mgr) const {
    auto& state = mgr.state();
    for (const auto& slot : slots_) {
        state.panels[std::string(slot.panel->id())] = slot.visible;
    }
}

void PanelHost::restoreFrom(const editor::LayoutManager& mgr) {
    const auto& state = mgr.state();
    for (auto& slot : slots_) {
        const auto it = state.panels.find(std::string(slot.panel->id()));
        if (it != state.panels.end()) {
            slot.visible = it->second;
        }
    }
}

} // namespace threadmaxx::studio
