#include "Check.hpp"

#include <cmath>
#include <string>

#include "threadmaxx_assets/loaders/obj.hpp"

#define STR2(x) #x
#define STR(x) STR2(x)

using namespace threadmaxx::assets;

int main() {
    const std::string path = std::string(STR(THREADMAXX_ASSETS_FIXTURES_DIR)) + "/cube.obj";

    auto r = loadObj(path);
    CHECK(r.ok());
    if (!r.ok()) {
        EXIT_WITH_RESULT();
    }

    const auto& m = r.value;
    CHECK_EQ(m.vertices.size(), std::size_t{24});       // 8 corners × 3 faces each
    CHECK_EQ(m.indices.size(),  std::size_t{36});       // 12 triangles
    CHECK_EQ(m.submeshes.size(), std::size_t{1});
    CHECK_EQ(m.submeshes[0].indexCount, std::uint32_t{36});

    // AABB pinned to the unit cube.
    CHECK(std::abs(m.aabb.min[0] + 0.5f) < 1e-5f);
    CHECK(std::abs(m.aabb.min[1] + 0.5f) < 1e-5f);
    CHECK(std::abs(m.aabb.min[2] + 0.5f) < 1e-5f);
    CHECK(std::abs(m.aabb.max[0] - 0.5f) < 1e-5f);
    CHECK(std::abs(m.aabb.max[1] - 0.5f) < 1e-5f);
    CHECK(std::abs(m.aabb.max[2] - 0.5f) < 1e-5f);

    // Every emitted vertex has a unit normal.
    for (const auto& v : m.vertices) {
        const float len = std::sqrt(
            v.normal[0] * v.normal[0] +
            v.normal[1] * v.normal[1] +
            v.normal[2] * v.normal[2]);
        CHECK(std::abs(len - 1.0f) < 1e-4f);
    }

    // Every index < vertices.size().
    for (auto i : m.indices) {
        CHECK(i < m.vertices.size());
    }

    // Source path round-tripped.
    CHECK_EQ(m.sourcePath, path);

    EXIT_WITH_RESULT();
}
