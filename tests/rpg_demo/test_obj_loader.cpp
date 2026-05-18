// §3.11 batch 9b.1 — Wavefront OBJ parser regression test.
//
// Exercises the sibling-library `rpg::parseObj` function from
// examples/rpg_demo/ObjLoader.cpp via the rpg_demo_core static lib.
// Inputs are inline `.obj` source strings to keep the test
// self-contained (no on-disk fixtures); the parser's `parseObjFile`
// disk path is covered by the demo's runtime usage and is not
// re-exercised here.
//
// Cases:
//   1. Single triangle (3 corners, 1 triangle, 3 indices, normal preserved).
//   2. Hand-rolled cube: 8 v + 6 vn + 6 quad faces = 12 triangles =
//      36 corners. Fan-split must produce exactly 36 indices.
//   3. n-gon (pentagon): one 5-corner face → 3 triangles = 9 corners.
//   4. Missing-normal fallback: no `vn` lines, face uses `a//b` shape
//      should fall back to a defaulted normal without failing parse.
//   5. Malformed-line tolerance: bad floats / unknown directives skipped.
//   6. Empty input fails cleanly with ok=false.

#include "Check.hpp"

#include "../../examples/rpg_demo/ObjLoader.hpp"

#include <cstdio>
#include <string_view>

int main() {
    using namespace rpg;

    // ---- 1. single triangle -----------------------------------------------
    {
        constexpr std::string_view src =
            "v 0.0 0.0 0.0\n"
            "v 1.0 0.0 0.0\n"
            "v 0.0 1.0 0.0\n"
            "vn 0.0 0.0 1.0\n"
            "f 1//1 2//1 3//1\n";
        const auto r = parseObj(src);
        CHECK(r.ok);
        CHECK_EQ(r.mesh.cornerCount, 3u);
        CHECK_EQ(r.mesh.indices.size(), std::size_t{3});
        CHECK_EQ(r.mesh.vertices.size(), std::size_t{18}); // 3 corners × 6 floats
        // First corner: pos=(0,0,0), normal=(0,0,1).
        CHECK_EQ(r.mesh.vertices[0], 0.0f);
        CHECK_EQ(r.mesh.vertices[5], 1.0f);
        std::printf("[obj_loader] single triangle OK\n");
    }

    // ---- 2. hand-rolled cube ----------------------------------------------
    {
        // 8 vertices + 6 normals + 6 quad faces. Each quad fan-splits
        // into 2 triangles → 12 triangles → 36 corners.
        constexpr std::string_view src =
            "v -0.5 -0.5 -0.5\n"
            "v  0.5 -0.5 -0.5\n"
            "v  0.5  0.5 -0.5\n"
            "v -0.5  0.5 -0.5\n"
            "v -0.5 -0.5  0.5\n"
            "v  0.5 -0.5  0.5\n"
            "v  0.5  0.5  0.5\n"
            "v -0.5  0.5  0.5\n"
            "vn  0  0 -1\n"
            "vn  0  0  1\n"
            "vn  0 -1  0\n"
            "vn  0  1  0\n"
            "vn -1  0  0\n"
            "vn  1  0  0\n"
            "f 1//1 2//1 3//1 4//1\n"   // -Z face
            "f 5//2 8//2 7//2 6//2\n"   // +Z face
            "f 1//3 5//3 6//3 2//3\n"   // -Y face
            "f 4//4 3//4 7//4 8//4\n"   // +Y face
            "f 1//5 4//5 8//5 5//5\n"   // -X face
            "f 2//6 6//6 7//6 3//6\n";  // +X face
        const auto r = parseObj(src);
        CHECK(r.ok);
        CHECK_EQ(r.mesh.cornerCount, 36u);
        CHECK_EQ(r.mesh.indices.size(), std::size_t{36});
        CHECK_EQ(r.mesh.vertices.size(), std::size_t{36 * 6});
        std::printf("[obj_loader] cube 36 corners OK\n");
    }

    // ---- 3. n-gon (pentagon) fan-split ------------------------------------
    {
        constexpr std::string_view src =
            "v 0.0  1.0 0\n"
            "v -1.0 0.3 0\n"
            "v -0.6 -0.8 0\n"
            "v  0.6 -0.8 0\n"
            "v  1.0 0.3 0\n"
            "vn 0 0 1\n"
            "f 1//1 2//1 3//1 4//1 5//1\n";
        const auto r = parseObj(src);
        CHECK(r.ok);
        // 5-gon → 3 triangles → 9 corners.
        CHECK_EQ(r.mesh.cornerCount, 9u);
        CHECK_EQ(r.mesh.indices.size(), std::size_t{9});
        std::printf("[obj_loader] pentagon → 3 triangles OK\n");
    }

    // ---- 4. missing-normal fallback ---------------------------------------
    {
        constexpr std::string_view src =
            "v 0 0 0\n"
            "v 1 0 0\n"
            "v 0 1 0\n"
            "f 1 2 3\n";   // No normal index — uses default (0,1,0).
        const auto r = parseObj(src);
        CHECK(r.ok);
        CHECK_EQ(r.mesh.cornerCount, 3u);
        // Corner 0's normal slot: index 3,4,5 of the vertex stream.
        CHECK_EQ(r.mesh.vertices[3], 0.0f);
        CHECK_EQ(r.mesh.vertices[4], 1.0f);
        CHECK_EQ(r.mesh.vertices[5], 0.0f);
        std::printf("[obj_loader] missing-normal fallback OK\n");
    }

    // ---- 5. malformed lines + unknown directives skipped ------------------
    {
        constexpr std::string_view src =
            "# comment line\n"
            "o cube\n"
            "mtllib does_not_exist.mtl\n"
            "g group\n"
            "s 1\n"
            "v 0 0 0\n"
            "v 1 0 0\n"
            "v 0 1 0\n"
            "v not_a_number 0 0\n"   // bad value — skipped, not fatal
            "vt 0.5 0.5\n"           // texture coord — parsed-then-ignored
            "vn 0 0 1\n"
            "usemtl wood\n"
            "f 1/1/1 2/1/1 3/1/1\n";
        const auto r = parseObj(src);
        CHECK(r.ok);
        CHECK_EQ(r.mesh.cornerCount, 3u);
        std::printf("[obj_loader] malformed lines tolerated OK\n");
    }

    // ---- 6. empty input ---------------------------------------------------
    {
        const auto r = parseObj("");
        CHECK(!r.ok);
        CHECK(!r.error.empty());
        std::printf("[obj_loader] empty input rejected: %s\n",
                    r.error.c_str());
    }

    // ---- 7. no-faces input ------------------------------------------------
    {
        constexpr std::string_view src =
            "v 0 0 0\n"
            "v 1 0 0\n"
            "v 0 1 0\n";  // No `f` line.
        const auto r = parseObj(src);
        CHECK(!r.ok);
        std::printf("[obj_loader] no-faces input rejected: %s\n",
                    r.error.c_str());
    }

    EXIT_WITH_RESULT();
}
