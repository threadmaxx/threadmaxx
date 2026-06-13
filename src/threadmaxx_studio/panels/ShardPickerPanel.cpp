/// @file panels/ShardPickerPanel.cpp
/// @brief ST34 — ShardPickerPanel implementation.

#include <threadmaxx_studio/panels/shard_picker.hpp>
#include <threadmaxx_studio/shard_directory.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <cstdio>

namespace threadmaxx::studio {

ShardPickerPanel::ShardPickerPanel(ShardDirectory& dir) noexcept
    : dir_(&dir) {}

bool ShardPickerPanel::pickShard(std::size_t index) {
    return dir_->select(index);
}

bool ShardPickerPanel::pickShardByName(std::string_view name) {
    return dir_->selectByName(name);
}

void ShardPickerPanel::clearSelection() noexcept {
    dir_->clearSelection();
}

void ShardPickerPanel::render(editor::IEditorBackend& backend,
                              IStudioDataSource&) {
    const auto shards = dir_->shards();
    const auto sel    = dir_->selectedIndex();

    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "Shards: %zu  alive=%zu  selected=%s",
                  shards.size(), dir_->aliveCount(),
                  sel.has_value() ? "yes" : "no");
    backend.drawText(buf, 0.0f, 0.0f);

    float y = 16.0f;
    for (std::size_t i = 0; i < shards.size(); ++i) {
        const auto& s = shards[i];
        const char marker = (sel.has_value() && *sel == i) ? '*' : ' ';
        std::snprintf(buf, sizeof(buf),
                      "%c [%zu] %-20.20s  host=%-16.16s  port=%u  %s",
                      marker, i, s.name.c_str(),
                      s.host.empty() ? "<loopback>" : s.host.c_str(),
                      static_cast<unsigned>(s.port),
                      s.alive ? "alive" : "down");
        backend.drawText(buf, 0.0f, y);
        y += 14.0f;
    }
    lastRows_ = 1 + shards.size();
}

} // namespace threadmaxx::studio
