#include "Check.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "threadmaxx_assets/loaders/ply.hpp"

using namespace threadmaxx::assets;

namespace {

void appendBytes(std::vector<std::byte>& dst, const void* p, std::size_t n) {
    const auto* b = static_cast<const std::byte*>(p);
    dst.insert(dst.end(), b, b + n);
}

template <class T>
void appendLE(std::vector<std::byte>& dst, T v) {
    appendBytes(dst, &v, sizeof(T));
}

std::vector<std::byte> buildTetraPly() {
    std::vector<std::byte> bytes;

    const char header[] =
        "ply\n"
        "format binary_little_endian 1.0\n"
        "element vertex 4\n"
        "property float x\n"
        "property float y\n"
        "property float z\n"
        "element face 4\n"
        "property list uchar int vertex_indices\n"
        "end_header\n";
    appendBytes(bytes, header, sizeof(header) - 1);

    const float verts[4][3] = {
        { 0.0f,  1.0f,  0.0f},
        {-1.0f, -1.0f,  1.0f},
        { 1.0f, -1.0f,  1.0f},
        { 0.0f, -1.0f, -1.0f},
    };
    for (const auto& v : verts) {
        appendLE<float>(bytes, v[0]);
        appendLE<float>(bytes, v[1]);
        appendLE<float>(bytes, v[2]);
    }

    const std::int32_t faces[4][3] = {
        {0, 1, 2},
        {0, 2, 3},
        {0, 3, 1},
        {1, 3, 2},
    };
    for (const auto& f : faces) {
        appendLE<std::uint8_t>(bytes, 3);
        appendLE<std::int32_t>(bytes, f[0]);
        appendLE<std::int32_t>(bytes, f[1]);
        appendLE<std::int32_t>(bytes, f[2]);
    }
    return bytes;
}

} // namespace

int main() {
    const auto bytes = buildTetraPly();
    auto r = parsePly(std::span<const std::byte>(bytes), "tetra.ply");
    CHECK(r.ok());
    if (!r.ok()) {
        EXIT_WITH_RESULT();
    }

    const auto& m = r.value;
    CHECK_EQ(m.vertices.size(), std::size_t{4});
    CHECK_EQ(m.indices.size(),  std::size_t{12});

    // First vertex is the apex.
    CHECK(std::abs(m.vertices[0].position[0] - 0.0f) < 1e-5f);
    CHECK(std::abs(m.vertices[0].position[1] - 1.0f) < 1e-5f);
    CHECK(std::abs(m.vertices[0].position[2] - 0.0f) < 1e-5f);

    // AABB.
    CHECK(std::abs(m.aabb.min[0] + 1.0f) < 1e-5f);
    CHECK(std::abs(m.aabb.max[0] - 1.0f) < 1e-5f);
    CHECK(std::abs(m.aabb.min[1] + 1.0f) < 1e-5f);
    CHECK(std::abs(m.aabb.max[1] - 1.0f) < 1e-5f);
    CHECK(std::abs(m.aabb.min[2] + 1.0f) < 1e-5f);
    CHECK(std::abs(m.aabb.max[2] - 1.0f) < 1e-5f);

    // Bad magic returns BadMagic.
    std::vector<std::byte> notPly = {std::byte{'n'}, std::byte{'o'}};
    auto bad = parsePly(notPly, "");
    CHECK(!bad.ok());
    CHECK(bad.code == ErrorCode::BadMagic || bad.code == ErrorCode::Truncated);

    EXIT_WITH_RESULT();
}
