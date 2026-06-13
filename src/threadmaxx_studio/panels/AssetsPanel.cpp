/// @file panels/AssetsPanel.cpp
/// @brief ST18 — `AssetsPanel` implementation.

#include <threadmaxx_studio/panels/assets.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx_assets/registry.hpp>
#include <threadmaxx_assets/types.hpp>

#include <cstdio>

namespace threadmaxx::studio {

namespace {

const char* typeName(assets::AssetType t) noexcept {
    switch (t) {
        case assets::AssetType::Unknown: return "?";
        case assets::AssetType::Mesh:    return "mesh";
        case assets::AssetType::Texture: return "texture";
        case assets::AssetType::Audio:   return "audio";
        case assets::AssetType::Font:    return "font";
        case assets::AssetType::Bundle:  return "bundle";
    }
    return "?";
}

} // namespace

AssetsPanel::AssetsPanel(assets::AssetRegistry& reg) noexcept
    : registry_(&reg) {}

void AssetsPanel::render(editor::IEditorBackend& backend,
                         IStudioDataSource&) {
    if (registry_ == nullptr) {
        backend.drawText("Assets: <detached>", 0.0f, 0.0f);
        lastRows_ = 0;
        return;
    }

    const auto rows = registry_->listResident();
    char header[96];
    std::snprintf(header, sizeof(header), "Assets  count=%zu", rows.size());
    backend.drawText(header, 0.0f, 0.0f);

    lastRows_ = rows.size();
    float y = 16.0f;
    const std::size_t cap = rows.size() < kMaxTrackedRows
                                ? rows.size() : kMaxTrackedRows;
    for (std::size_t i = 0; i < cap; ++i) rowIds_[i] = rows[i].id;
    for (std::size_t i = cap; i < kMaxTrackedRows; ++i) rowIds_[i] = 0;

    for (const auto& r : rows) {
        char row[160];
        std::snprintf(row, sizeof(row),
                      "id=%u  type=%-7.7s  refs=%u  %s",
                      r.id, typeName(r.type), r.refCount, r.path.c_str());
        backend.drawText(row, 0.0f, y);
        y += 14.0f;
    }
}

bool AssetsPanel::reloadRow(std::size_t index) {
    if (registry_ == nullptr) return false;
    if (index >= lastRows_ || index >= kMaxTrackedRows) return false;
    return registry_->reload(rowIds_[index]);
}

std::uint32_t AssetsPanel::rowAssetId(std::size_t index) const noexcept {
    if (index >= lastRows_ || index >= kMaxTrackedRows) return 0;
    return rowIds_[index];
}

} // namespace threadmaxx::studio
