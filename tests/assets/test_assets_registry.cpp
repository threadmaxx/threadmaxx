#include "Check.hpp"

#include <string>
#include <utility>

#include "threadmaxx_assets/data/mesh.hpp"
#include "threadmaxx_assets/registry.hpp"

#define STR2(x) #x
#define STR(x) STR2(x)

using namespace threadmaxx::assets;

int main() {
    const std::string cubePath =
        std::string(STR(THREADMAXX_ASSETS_FIXTURES_DIR)) + "/cube.obj";

    // Dedup: two loads → same id, refcount == 2.
    {
        AssetRegistry reg;
        auto a = reg.loadMesh(cubePath);
        auto b = reg.loadMesh(cubePath);
        CHECK(a.valid());
        CHECK(b.valid());
        CHECK_EQ(a.id(), b.id());
        CHECK_EQ(reg.refCount(a.id()), 2u);
        // Pointer identity: both handles see the same underlying POD.
        CHECK(a.get() == b.get());
        CHECK_EQ(reg.liveAssetCount(AssetType::Mesh), std::size_t{1});

        const auto s = reg.stats();
        CHECK_EQ(s.loadsSync,  1ull);
        CHECK_EQ(s.loadsDedup, 1ull);
    }

    // RAII: dropping all handles evicts the slot.
    {
        AssetRegistry reg;
        AssetId id = kInvalidAssetId;
        {
            auto h = reg.loadMesh(cubePath);
            CHECK(h.valid());
            id = h.id();
            CHECK_EQ(reg.refCount(id), 1u);
        }
        CHECK_EQ(reg.refCount(id), 0u);
        CHECK_EQ(reg.liveAssetCount(AssetType::Mesh), std::size_t{0});
        CHECK_EQ(reg.stats().evicted, 1ull);
    }

    // addMesh + pathOf round-trip.
    {
        AssetRegistry reg;
        MeshData m{};
        m.vertices.push_back(MeshVertex{{0,0,0},{0,0,1},{0,0}});
        m.indices = {0u};
        auto h = reg.addMesh("procedural/triangle", std::move(m));
        CHECK(h.valid());
        CHECK_EQ(reg.typeOf(h.id()), AssetType::Mesh);
        auto p = reg.pathOf(h.id());
        CHECK(p.has_value());
        CHECK_EQ(*p, "procedural/triangle");
        // Injected slot is NOT file-backed → reload must fail.
        CHECK(!reg.reload(h.id()));
    }

    // reload: handle stays valid, refcount preserved.
    {
        AssetRegistry reg;
        auto h = reg.loadMesh(cubePath);
        CHECK(h.valid());
        const auto idBefore = h.id();
        const auto rcBefore = reg.refCount(idBefore);
        const auto* ptrBefore = h.get();
        CHECK(reg.reload(idBefore));
        CHECK(h.valid());
        CHECK_EQ(h.id(), idBefore);
        CHECK_EQ(reg.refCount(idBefore), rcBefore);
        // The reloaded slot must have a fresh POD pointer (new allocation).
        const auto* ptrAfter = h.get();
        CHECK(ptrBefore != ptrAfter);
        CHECK_EQ(reg.stats().reloads, 1ull);
    }

    EXIT_WITH_RESULT();
}
