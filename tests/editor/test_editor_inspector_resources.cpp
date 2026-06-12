/// @file test_editor_inspector_resources.cpp
/// @brief E2 — track 3 resource ids via the inspector; listResources()
/// reports their current refCounts.

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/inspect.hpp>

namespace {

struct Mesh { int triangles{0}; };
struct Texture { int width{0}; int height{0}; };

} // namespace

int main() {
    threadmaxx::editor::test::ScopedEngine env;
    auto& reg = env.engine().resources();

    auto mesh1 = reg.addRefCounted<Mesh>(Mesh{100});
    auto mesh2 = reg.addRefCounted<Mesh>(Mesh{200});
    auto tex1  = reg.addRefCounted<Texture>(Texture{256, 256});

    threadmaxx::editor::Inspector ins{env.engine()};
    ins.trackResource(mesh1.id(), "mesh_low.obj");
    ins.trackResource(mesh2.id(), "mesh_high.obj");
    ins.trackResource(tex1.id(),  "diffuse.png");
    CHECK_EQ(ins.trackedResourceCount(), 3u);

    const auto rows = ins.listResources();
    CHECK_EQ(rows.size(), 3u);

    bool sawMesh1 = false, sawMesh2 = false, sawTex = false;
    for (const auto& row : rows) {
        if (row.name == "mesh_low.obj")  { sawMesh1 = true; CHECK_EQ(row.refCount, 1u); CHECK(!row.stale); }
        if (row.name == "mesh_high.obj") { sawMesh2 = true; CHECK_EQ(row.refCount, 1u); CHECK(!row.stale); }
        if (row.name == "diffuse.png")   { sawTex   = true; CHECK_EQ(row.refCount, 1u); CHECK(!row.stale); }
    }
    CHECK(sawMesh1); CHECK(sawMesh2); CHECK(sawTex);

    // Make a second handle on mesh1 → refcount bumps to 2.
    auto mesh1Copy = mesh1;
    const auto rows2 = ins.listResources();
    for (const auto& row : rows2) {
        if (row.name == "mesh_low.obj") {
            CHECK_EQ(row.refCount, 2u);
        }
    }
    (void)mesh1Copy;

    EXIT_WITH_RESULT();
}
