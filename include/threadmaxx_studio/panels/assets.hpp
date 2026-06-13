#pragma once

/// @file panels/assets.hpp
/// @brief ST18 — `AssetsPanel` enumerates an `assets::AssetRegistry`
/// via the A9-resident accessor (`listResident`). Each row shows
/// the asset id, type, refcount, and canonical path. `reload(id)`
/// triggers a reload through the registry's existing surface.

#include "../panel.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace threadmaxx::assets {
class AssetRegistry;
enum class AssetType : std::uint8_t;
} // namespace threadmaxx::assets

namespace threadmaxx::studio {

class AssetsPanel : public IStudioPanel {
public:
    AssetsPanel() noexcept = default;
    explicit AssetsPanel(assets::AssetRegistry& registry) noexcept;

    void setRegistry(assets::AssetRegistry* reg) noexcept { registry_ = reg; }
    [[nodiscard]] assets::AssetRegistry* registry() const noexcept {
        return registry_;
    }

    std::string_view id() const noexcept override { return "sibling.assets"; }
    std::string_view title() const noexcept override { return "Assets"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief Asset row count from the most recent render.
    [[nodiscard]] std::size_t assetRowCount() const noexcept { return lastRows_; }

    /// @brief Trigger a registry reload for the asset id at row
    /// @p index. Returns false on out-of-range or unbound registry.
    bool reloadRow(std::size_t index);

    /// @brief Asset id stored at the given row, or 0 if out of range.
    [[nodiscard]] std::uint32_t rowAssetId(std::size_t index) const noexcept;

private:
    assets::AssetRegistry* registry_{nullptr};
    std::size_t            lastRows_{0};
    static constexpr std::size_t kMaxTrackedRows = 64;
    std::uint32_t          rowIds_[kMaxTrackedRows]{};
};

} // namespace threadmaxx::studio
