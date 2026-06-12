/// @file panels/WorldDiffPanel.cpp

#include <threadmaxx_studio/panels/world_diff.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <cstdio>

namespace threadmaxx::studio {

namespace {

const char* kindLabel(editor::DiffKind k) noexcept {
    switch (k) {
        case editor::DiffKind::Added:    return "Added";
        case editor::DiffKind::Removed:  return "Removed";
        case editor::DiffKind::Modified: return "Modified";
    }
    return "?";
}

} // namespace

void WorldDiffPanel::render(editor::IEditorBackend& backend,
                            IStudioDataSource&) {
    if (lastDiff_.entries.empty()) {
        backend.drawText("(no diff)", 0.0f, 0.0f);
        return;
    }
    float y = 0.0f;
    for (const auto& e : lastDiff_.entries) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%-9s slot=%u gen=%u changes=%zu",
                      kindLabel(e.kind), e.handle.index, e.handle.generation,
                      e.componentChanges.size());
        backend.drawText(buf, 0.0f, y);
        y += 16.0f;
    }
}

void WorldDiffPanel::setSnapshot(std::string_view name,
                                 threadmaxx::WorldSnapshot snap) {
    for (auto& s : slots_) {
        if (s.name == name) {
            s.snap = std::move(snap);
            return;
        }
    }
    slots_.push_back(Slot{std::string(name), std::move(snap)});
}

bool WorldDiffPanel::computeDiff(std::string_view fromSlot,
                                 std::string_view toSlot) {
    const auto* a = findSlot(fromSlot);
    const auto* b = findSlot(toSlot);
    if (a == nullptr || b == nullptr) {
        return false;
    }
    lastDiff_ = editor::WorldDiff::compute(*a, *b);
    return true;
}

const threadmaxx::WorldSnapshot*
WorldDiffPanel::findSlot(std::string_view name) const noexcept {
    for (const auto& s : slots_) {
        if (s.name == name) {
            return &s.snap;
        }
    }
    return nullptr;
}

} // namespace threadmaxx::studio
