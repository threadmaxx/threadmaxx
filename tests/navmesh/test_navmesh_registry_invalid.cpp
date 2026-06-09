#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/threadmaxx_navmesh.hpp"

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

int main() {
    NavMeshRegistry reg;

    // 1. Empty blob.
    {
        NavMeshRef r = reg.load(std::span<const std::byte>{});
        CHECK(!r);
        CHECK(reg.lastLoadError() == NavMeshLoadError::EmptyBlob);
    }

    // 2. Wrong magic.
    {
        auto blob = make16PolyFlatSquare(/*magic=*/0xDEADBEEFu);
        NavMeshRef r = reg.load(bytes(blob));
        CHECK(!r);
        CHECK(reg.lastLoadError() == NavMeshLoadError::InvalidMagic);
    }

    // 3. Unsupported version.
    {
        auto blob = make16PolyFlatSquare(kNavMeshBlobMagic, /*version=*/999u);
        NavMeshRef r = reg.load(bytes(blob));
        CHECK(!r);
        CHECK(reg.lastLoadError() == NavMeshLoadError::UnsupportedVersion);
    }

    // 4. Truncated mid-payload — chop the blob just past the version
    //    field so the reader runs out of bytes inside the tile section.
    {
        auto blob = make16PolyFlatSquare();
        std::vector<unsigned char> truncated(blob.begin(),
                                             blob.begin() + 8);
        NavMeshRef r = reg.load(bytes(truncated));
        CHECK(!r);
        CHECK(reg.lastLoadError() == NavMeshLoadError::Truncated);
    }

    // 5. After a failure, a fresh good load still works — the failure
    //    didn't leave the registry in a poisoned state.
    {
        auto blob = make16PolyFlatSquare();
        NavMeshRef r = reg.load(bytes(blob));
        CHECK(static_cast<bool>(r));
        CHECK(reg.lastLoadError() == NavMeshLoadError::None);
        CHECK(reg.isValid(r));
    }

    // 6. Garbage tile-count (above the cap) → InvalidTileCount.
    {
        BlobBuilder b;
        b.writeU32(kNavMeshBlobMagic);
        b.writeU32(kNavMeshBlobVersion);
        b.writeString("oversize");
        b.writeU32(kNavMeshMaxTiles + 1);  // overshoots the cap
        auto view = b.view();
        NavMeshRef r = reg.load(view);
        CHECK(!r);
        CHECK(reg.lastLoadError() == NavMeshLoadError::InvalidTileCount);
    }

    EXIT_WITH_RESULT();
}
