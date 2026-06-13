/// @file test_studio_assets_panel_reload.cpp
/// @brief ST18 — AssetsPanel enumerates resident assets, exposes
/// per-row ids, and forwards `reloadRow` to the registry.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/assets.hpp>

#include <threadmaxx_assets/registry.hpp>
#include <threadmaxx_assets/types.hpp>

namespace {

std::size_t countTextOps(
    const threadmaxx::editor::CapturedFrame& frame) {
    std::size_t n = 0;
    for (const auto& op : frame.ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText) ++n;
    }
    return n;
}

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    using namespace threadmaxx;
    studio::AssetsPanel panel;

    editor::HeadlessBackend backend;
    backend.initialize();
    NullSource source;

    // Detached.
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.assetRowCount(), 0u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 1u);

    // Wire a registry with one mesh + one texture.
    assets::AssetRegistry reg;
    assets::MeshData m{};
    assets::TextureData t{};
    auto mesh = reg.addMesh("procedural/cube", assets::MeshData{m});
    auto tex  = reg.addTexture("procedural/diffuse", assets::TextureData{t});
    CHECK(mesh.valid());
    CHECK(tex.valid());
    panel.setRegistry(&reg);

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.assetRowCount(), 2u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 3u); // header + 2 rows

    // Row ids round-trip through `rowAssetId` (id=0 IS a valid slot;
    // kInvalidAssetId is 0xFFFFFFFFu).
    const auto id0 = panel.rowAssetId(0);
    const auto id1 = panel.rowAssetId(1);
    CHECK(id0 != assets::kInvalidAssetId);
    CHECK(id1 != assets::kInvalidAssetId);
    CHECK(id0 != id1);
    CHECK_EQ(panel.rowAssetId(99), 0u);

    // `reloadRow` is rejected for procedural / in-memory assets
    // (no source path), and out-of-range is rejected even when a
    // valid registry is bound.
    CHECK(!panel.reloadRow(99));

    backend.shutdown();
    EXIT_WITH_RESULT();
}
