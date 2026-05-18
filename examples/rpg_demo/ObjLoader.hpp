// §3.11 batch 9b.1 — sibling-library OBJ parser (demo-side).
//
// Lives in examples/rpg_demo/ per FUTURE_WORK §3.3 sibling-library
// policy: real `.obj` parsing is game-side, never engine-side. The
// parser is pure CPU — no Vulkan headers, no engine headers — so a
// future renderer-side `MeshLoader::createFromObj(engine, path)`
// integration can call it without dragging in either dependency.
//
// Scope: Wavefront OBJ subset sufficient for the demo's authoring
// pipeline. Supports `v x y z [w]`, `vn x y z`, `vt u v` (parsed +
// discarded — the demo's opaque pipeline doesn't sample textures
// yet), `f a/b/c d/e/f g/h/i [...]` with n-gon faces fan-split into
// triangles. Negative vertex indices are NOT supported (rare in
// hand-written / Blender-exported files). Per-face material
// (`usemtl` / `mtllib`) is parsed-then-ignored.
//
// Output shape: an expanded, per-face-corner vertex array. The
// renderer's opaque pipeline binds position[3] + normal[3] at
// binding 0 with a 24-byte stride (see `MeshLoader.cpp`); this
// parser produces exactly that layout. No deduplication — flat
// shading is preserved because each face corner gets its own
// per-face normal.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rpg {

/// Parsed OBJ data ready to upload into a `VkBuffer` pair. Layout
/// matches the opaque pipeline's vertex binding: vertices is a flat
/// stream of {pos.x, pos.y, pos.z, normal.x, normal.y, normal.z}
/// triples, six floats per corner. Indices are 16-bit; the parser
/// refuses to produce a mesh that would overflow.
struct MeshData {
    std::vector<float>          vertices;  ///< 6 floats per corner: posXYZ + normalXYZ
    std::vector<std::uint16_t>  indices;
    /// Distinct corners written. `vertices.size() == 6 * cornerCount`
    /// and `indices.size() == cornerCount`.
    std::uint32_t               cornerCount = 0;
};

/// Outcome of a parse attempt. `ok=false` carries a one-line
/// human-readable reason. The library never throws on malformed
/// input — bad lines are skipped silently except for hard structural
/// failures (overflow / no faces).
struct ObjParseResult {
    MeshData    mesh;
    bool        ok      = false;
    std::string error;
};

/// Parse a Wavefront OBJ source. The input is a `string_view` so
/// callers can feed either a `std::string` loaded from disk or an
/// inline test fixture without copying.
ObjParseResult parseObj(std::string_view source) noexcept;

/// Convenience: read a file from disk via `std::ifstream` and feed
/// the contents to @ref parseObj. Returns `ok=false` if the file
/// can't be opened or is empty; the underlying parse error
/// propagates through unchanged.
ObjParseResult parseObjFile(std::string_view path) noexcept;

} // namespace rpg
