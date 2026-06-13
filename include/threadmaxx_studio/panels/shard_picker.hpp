#pragma once

/// @file panels/shard_picker.hpp
/// @brief ST34 — `ShardPickerPanel`: list shards from a
/// `ShardDirectory`, highlight the current selection, and pick by
/// row index. Read/write surface — selection mutations bypass the
/// data source since they are studio-local state.

#include "../panel.hpp"

#include <cstddef>
#include <string_view>

namespace threadmaxx::studio {

class ShardDirectory;

class ShardPickerPanel : public IStudioPanel {
public:
    explicit ShardPickerPanel(ShardDirectory& dir) noexcept;

    /// @brief Pick the shard at @p index. Forwards to
    /// `ShardDirectory::select`; returns true on success.
    bool pickShard(std::size_t index);

    /// @brief Pick by name. Forwards to `ShardDirectory::selectByName`.
    bool pickShardByName(std::string_view name);

    /// @brief Drop the current selection.
    void clearSelection() noexcept;

    [[nodiscard]] const ShardDirectory& directory() const noexcept {
        return *dir_;
    }

    std::string_view id() const noexcept override {
        return "sibling.shard_picker";
    }
    std::string_view title() const noexcept override { return "Shards"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    [[nodiscard]] std::size_t rowCount() const noexcept { return lastRows_; }

private:
    ShardDirectory* dir_;
    std::size_t     lastRows_{0};
};

} // namespace threadmaxx::studio
