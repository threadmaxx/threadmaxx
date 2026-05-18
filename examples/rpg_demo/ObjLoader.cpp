/// @file ObjLoader.cpp
/// §3.11 batch 9b.1 — Wavefront OBJ parser (demo-side).
///
/// Implementation strategy: linear scan, one pass over the input.
/// We accumulate per-source-line `v`, `vn` into vectors keyed by
/// 1-based OBJ index (slot 0 is wasted). Each `f` line is split by
/// whitespace; each token's `pos/uv/normal` triplet (with `uv` and
/// `normal` optional) is parsed and recorded as a corner. N-gon
/// faces fan-split: corners (c0, c1, c2, c3, ...) become triangles
/// (c0, c1, c2), (c0, c2, c3), ... Unknown directives (`vt`,
/// `usemtl`, `mtllib`, `g`, `o`, `s`, comments) are silently
/// skipped. Lines with parse errors at the value level (e.g. `v
/// foo bar baz`) skip the offending line but don't fail the parse;
/// only structural failures (16-bit index overflow, no faces)
/// surface as `ok=false`.

#include "ObjLoader.hpp"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace rpg {

namespace {

struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

// Whitespace-skip helpers. We avoid `std::istringstream` per line —
// the token loop is hot enough that the iostream overhead shows up
// on a 100k-vertex mesh.
constexpr bool isSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r';
}

// Parse one float. Falls back to `strtof` because `from_chars(float)`
// requires libstdc++ 11+ for full support; `strtof` is universally
// available and the parser is not on a hot path.
bool parseFloat(std::string_view tok, float& out) noexcept {
    if (tok.empty()) return false;
    // strtof needs a NUL-terminated buffer; the token is bounded so
    // a fixed-size stack buffer is safe.
    char buf[64];
    if (tok.size() >= sizeof(buf)) return false;
    std::memcpy(buf, tok.data(), tok.size());
    buf[tok.size()] = '\0';
    char* end = nullptr;
    const float v = std::strtof(buf, &end);
    if (end == buf) return false;
    out = v;
    return true;
}

bool parseInt(std::string_view tok, int& out) noexcept {
    if (tok.empty()) return false;
    int v = 0;
    auto [ptr, ec] = std::from_chars(tok.data(), tok.data() + tok.size(), v);
    if (ec != std::errc{}) return false;
    out = v;
    return true;
}

// Split a single OBJ face-corner token `"a/b/c"` (or `"a"` or
// `"a//c"` or `"a/b"`) into 1-based vertex / uv / normal indices.
// uv and normal default to 0 (unset). Returns false if the position
// field is missing.
struct FaceCorner {
    int v  = 0;
    int vt = 0;
    int vn = 0;
};
bool parseFaceCorner(std::string_view tok, FaceCorner& out) noexcept {
    FaceCorner c{};
    // Split on '/'.
    std::size_t start = 0;
    int slot = 0;
    auto commit = [&](std::string_view piece) -> bool {
        if (piece.empty()) { ++slot; return true; }
        int v = 0;
        if (!parseInt(piece, v)) return false;
        if      (slot == 0) c.v  = v;
        else if (slot == 1) c.vt = v;
        else if (slot == 2) c.vn = v;
        ++slot;
        return true;
    };
    for (std::size_t i = 0; i < tok.size(); ++i) {
        if (tok[i] == '/') {
            if (!commit(tok.substr(start, i - start))) return false;
            start = i + 1;
        }
    }
    if (!commit(tok.substr(start))) return false;
    if (c.v == 0) return false;
    out = c;
    return true;
}

// Split a line into whitespace-separated tokens, dropping the
// directive prefix in `tokens[0]`.
void tokenize(std::string_view line, std::vector<std::string_view>& out) {
    out.clear();
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && isSpace(line[i])) ++i;
        if (i >= line.size()) break;
        const std::size_t start = i;
        while (i < line.size() && !isSpace(line[i])) ++i;
        out.emplace_back(line.substr(start, i - start));
    }
}

} // namespace

ObjParseResult parseObj(std::string_view source) noexcept {
    ObjParseResult result;

    // 1-based: slot 0 is a sentinel so `positions[1]` matches OBJ's
    // first `v` line.
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    positions.push_back(Vec3{});
    normals.push_back(Vec3{});

    // Final per-corner stream. We append corners as faces are
    // processed and write the parallel index immediately.
    auto& vbuf = result.mesh.vertices;
    auto& ibuf = result.mesh.indices;
    vbuf.reserve(source.size() / 32);
    ibuf.reserve(source.size() / 32);

    std::vector<std::string_view> tokens;
    std::vector<FaceCorner>       faceCorners;
    std::uint32_t                 cornerCount = 0;
    std::size_t                   lineStart   = 0;

    auto pushCorner = [&](const FaceCorner& fc) -> bool {
        // Validate position index.
        if (fc.v <= 0 || static_cast<std::size_t>(fc.v) >= positions.size()) {
            return false;
        }
        // Normal is optional. If missing, default to (0,1,0) to
        // keep the vertex stream well-formed; the demo's flat-lit
        // shader will still render a visible surface. Most OBJ
        // exporters include normals.
        Vec3 n{0.0f, 1.0f, 0.0f};
        if (fc.vn > 0 && static_cast<std::size_t>(fc.vn) < normals.size()) {
            n = normals[static_cast<std::size_t>(fc.vn)];
        }
        const Vec3 p = positions[static_cast<std::size_t>(fc.v)];

        if (cornerCount >= 0xFFFFu) {
            return false;  // 16-bit index overflow guard.
        }
        vbuf.push_back(p.x); vbuf.push_back(p.y); vbuf.push_back(p.z);
        vbuf.push_back(n.x); vbuf.push_back(n.y); vbuf.push_back(n.z);
        ibuf.push_back(static_cast<std::uint16_t>(cornerCount));
        ++cornerCount;
        return true;
    };

    while (lineStart <= source.size()) {
        // Find line end.
        std::size_t lineEnd = lineStart;
        while (lineEnd < source.size() && source[lineEnd] != '\n') ++lineEnd;
        std::string_view line = source.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        // Strip comment.
        const auto hashPos = line.find('#');
        if (hashPos != std::string_view::npos) line = line.substr(0, hashPos);

        tokenize(line, tokens);
        if (tokens.empty()) {
            if (lineEnd >= source.size()) break;
            continue;
        }

        const auto dir = tokens[0];
        if (dir == "v" && tokens.size() >= 4) {
            Vec3 v{};
            if (parseFloat(tokens[1], v.x) &&
                parseFloat(tokens[2], v.y) &&
                parseFloat(tokens[3], v.z)) {
                positions.push_back(v);
            }
        } else if (dir == "vn" && tokens.size() >= 4) {
            Vec3 n{};
            if (parseFloat(tokens[1], n.x) &&
                parseFloat(tokens[2], n.y) &&
                parseFloat(tokens[3], n.z)) {
                normals.push_back(n);
            }
        } else if (dir == "f" && tokens.size() >= 4) {
            faceCorners.clear();
            for (std::size_t i = 1; i < tokens.size(); ++i) {
                FaceCorner fc{};
                if (parseFaceCorner(tokens[i], fc)) {
                    faceCorners.push_back(fc);
                }
            }
            if (faceCorners.size() < 3) continue;

            // Triangle-fan split: (c0, c1, c2), (c0, c2, c3), ...
            // Each triangle pushes three corners + three indices.
            for (std::size_t i = 1; i + 1 < faceCorners.size(); ++i) {
                if (!pushCorner(faceCorners[0])      ||
                    !pushCorner(faceCorners[i])      ||
                    !pushCorner(faceCorners[i + 1])) {
                    result.ok    = false;
                    result.error = "16-bit index overflow (>65535 corners)";
                    return result;
                }
            }
        }
        // `vt`, `usemtl`, `mtllib`, `g`, `o`, `s`, `l`, etc. are
        // silently skipped — they don't contribute to the opaque-
        // pipeline vertex stream.

        if (lineEnd >= source.size()) break;
    }

    if (cornerCount < 3) {
        result.ok    = false;
        result.error = "no triangle faces parsed";
        return result;
    }

    result.mesh.cornerCount = cornerCount;
    result.ok               = true;
    return result;
}

ObjParseResult parseObjFile(std::string_view path) noexcept {
    ObjParseResult result;
    // `std::ifstream` ctor wants a `const char*` / `std::string`; copy
    // the view into a NUL-terminated buffer.
    std::string pathStr(path);
    std::ifstream f(pathStr, std::ios::binary);
    if (!f.is_open()) {
        result.error = "could not open file";
        return result;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string contents = ss.str();
    if (contents.empty()) {
        result.error = "file empty";
        return result;
    }
    return parseObj(contents);
}

} // namespace rpg
