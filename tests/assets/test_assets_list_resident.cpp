/// @file test_assets_list_resident.cpp
/// @brief A9-resident — `AssetRegistry::listResident` enumerates every
/// live slot, exposing AssetId / type / refCount / canonical path.
/// Filtering by type narrows the list.

#include "Check.hpp"

#include <threadmaxx_assets/registry.hpp>

int main() {
    using namespace threadmaxx::assets;

    AssetRegistry reg;

    // Empty baseline.
    {
        const auto rows = reg.listResident();
        CHECK_EQ(rows.size(), 0u);
    }

    // Mix of meshes + textures.
    MeshData m{};
    auto mesh1 = reg.addMesh("procedural/cube", MeshData{m});
    auto mesh2 = reg.addMesh("procedural/sphere", MeshData{m});
    TextureData t{};
    auto tex1 = reg.addTexture("procedural/diffuse", TextureData{t});

    CHECK(mesh1.valid());
    CHECK(mesh2.valid());
    CHECK(tex1.valid());

    // Unfiltered listing: all three live slots present.
    {
        const auto rows = reg.listResident();
        CHECK_EQ(rows.size(), 3u);
        bool sawCube = false, sawSphere = false, sawTex = false;
        for (const auto& r : rows) {
            CHECK(r.refCount >= 1u);
            if (r.path == "procedural/cube") {
                sawCube = true;
                CHECK(r.type == AssetType::Mesh);
            } else if (r.path == "procedural/sphere") {
                sawSphere = true;
                CHECK(r.type == AssetType::Mesh);
            } else if (r.path == "procedural/diffuse") {
                sawTex = true;
                CHECK(r.type == AssetType::Texture);
            }
        }
        CHECK(sawCube);
        CHECK(sawSphere);
        CHECK(sawTex);
    }

    // Filter by type.
    {
        const auto rows = reg.listResident(AssetType::Mesh);
        CHECK_EQ(rows.size(), 2u);
        for (const auto& r : rows) {
            CHECK(r.type == AssetType::Mesh);
        }
    }
    {
        const auto rows = reg.listResident(AssetType::Texture);
        CHECK_EQ(rows.size(), 1u);
        CHECK(rows[0].type == AssetType::Texture);
        CHECK(rows[0].path == "procedural/diffuse");
    }

    // Refcount bump shows through.
    auto mesh1Copy = mesh1;
    {
        const auto rows = reg.listResident();
        for (const auto& r : rows) {
            if (r.path == "procedural/cube") {
                CHECK_EQ(r.refCount, 2u);
            }
        }
    }
    (void)mesh1Copy;

    EXIT_WITH_RESULT();
}
