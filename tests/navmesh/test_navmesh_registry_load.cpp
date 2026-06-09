#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/threadmaxx_navmesh.hpp"

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

int main() {
    NavMeshRegistry reg;
    CHECK_EQ(reg.size(), std::size_t{0});

    auto blob = make16PolyFlatSquare();
    NavMeshRef ref = reg.load(bytes(blob));

    // Successful load returns a valid ref.
    CHECK(static_cast<bool>(ref));
    CHECK(reg.isValid(ref));
    CHECK_EQ(reg.size(), std::size_t{1});
    CHECK(reg.lastLoadError() == NavMeshLoadError::None);

    // Meta reports the expected counts.
    auto m = reg.meta(ref);
    CHECK(m.has_value());
    CHECK_EQ(m->tileCount, std::uint32_t{1});
    CHECK_EQ(m->polygonCount, std::uint32_t{16});
    CHECK_EQ(m->vertexCount, std::uint32_t{25});
    CHECK(m->name == "flat_square_16");

    // find() returns a live pointer with the expected interior.
    const NavMesh* mesh = reg.find(ref);
    CHECK(mesh != nullptr);
    CHECK_EQ(mesh->tiles().size(), std::size_t{1});
    const NavTile& tile = mesh->tiles().front();
    CHECK_EQ(tile.polygons.size(), std::size_t{16});
    CHECK_EQ(tile.vertices.size(), std::size_t{25});
    // Every polygon claims 4 vertex indices on the flat-square fixture.
    for (const NavPoly& p : tile.polygons) {
        CHECK_EQ(p.indexCount, std::uint16_t{4});
        CHECK(p.indexStart + p.indexCount <= tile.vertexIndices.size());
    }

    // AABB is recomputed from the vertex pool — [0,0,0]..[4,0,4].
    CHECK(tile.aabbMin.x == 0.0f);
    CHECK(tile.aabbMin.y == 0.0f);
    CHECK(tile.aabbMin.z == 0.0f);
    CHECK(tile.aabbMax.x == 4.0f);
    CHECK(tile.aabbMax.y == 0.0f);
    CHECK(tile.aabbMax.z == 4.0f);

    // findTile() agrees with the tile id we baked.
    CHECK(mesh->findTile(0) == &tile);
    CHECK(mesh->findTile(99) == nullptr);

    EXIT_WITH_RESULT();
}
