#include "Check.hpp"

#include <cmath>
#include <cstddef>
#include <span>
#include <string_view>

#include "threadmaxx_assets/loaders/obj.hpp"

using namespace threadmaxx::assets;

// OBJ source: tetrahedron with NO `vn` lines. The smoothed-normals
// fallback must emit unit-length per-vertex normals derived from
// adjacent face normals.
static constexpr std::string_view kSource = R"(
v  0.0  1.0  0.0
v -1.0 -1.0  1.0
v  1.0 -1.0  1.0
v  0.0 -1.0 -1.0
f 1 2 3
f 1 3 4
f 1 4 2
f 2 4 3
)";

int main() {
    auto bytes = std::as_bytes(std::span<const char>(kSource.data(), kSource.size()));
    auto r = parseObj(bytes, "tetra.obj");
    CHECK(r.ok());

    const auto& m = r.value;
    CHECK_EQ(m.vertices.size(), std::size_t{4});
    CHECK_EQ(m.indices.size(),  std::size_t{12});

    for (const auto& v : m.vertices) {
        const float len = std::sqrt(
            v.normal[0] * v.normal[0] +
            v.normal[1] * v.normal[1] +
            v.normal[2] * v.normal[2]);
        CHECK(std::abs(len - 1.0f) < 1e-4f);
    }

    // Apex (vertex index 0 = world (0, 1, 0)) is shared by three faces
    // whose normals all point "outward-upward"; the smoothed normal must
    // have a positive y component.
    CHECK(m.vertices[0].normal[1] > 0.3f);

    EXIT_WITH_RESULT();
}
