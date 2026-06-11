/// @file backends/VertexBackend.cpp
/// @brief Reference backend tessellation. Walks the DrawList command stream
/// and produces flat vertex + index + draw-call buffers.

#include "threadmaxx_ui/backends/VertexBackend.hpp"

#include <cstddef>
#include <cstdint>

#include "threadmaxx_ui/draw.hpp"

namespace threadmaxx::ui {

namespace {

constexpr std::size_t kInitialVertices = 4096;
constexpr std::size_t kInitialIndices  = 6144;
constexpr std::size_t kInitialDraws    = 512;

inline void pushQuad(std::vector<VertexBackend::Vertex>& v,
                     std::vector<std::uint32_t>& idx,
                     float x0, float y0, float x1, float y1,
                     float u0, float v0, float u1, float v1,
                     Color c) noexcept {
    const std::uint32_t baseIdx = static_cast<std::uint32_t>(v.size());
    v.push_back({x0, y0, u0, v0, c.r, c.g, c.b, c.a});
    v.push_back({x1, y0, u1, v0, c.r, c.g, c.b, c.a});
    v.push_back({x1, y1, u1, v1, c.r, c.g, c.b, c.a});
    v.push_back({x0, y1, u0, v1, c.r, c.g, c.b, c.a});
    idx.push_back(baseIdx + 0);
    idx.push_back(baseIdx + 1);
    idx.push_back(baseIdx + 2);
    idx.push_back(baseIdx + 0);
    idx.push_back(baseIdx + 2);
    idx.push_back(baseIdx + 3);
}

inline void pushOutlineRect(std::vector<VertexBackend::Vertex>& v,
                            std::vector<std::uint32_t>& idx,
                            Rect r, std::int32_t thickness, Color c) noexcept {
    const float t = static_cast<float>(thickness);
    const float x0 = static_cast<float>(r.x);
    const float y0 = static_cast<float>(r.y);
    const float x1 = static_cast<float>(r.x + r.w);
    const float y1 = static_cast<float>(r.y + r.h);
    // Four sides as thin rects.
    pushQuad(v, idx, x0, y0, x1, y0 + t, 0, 0, 0, 0, c);          // top
    pushQuad(v, idx, x0, y1 - t, x1, y1, 0, 0, 0, 0, c);          // bottom
    pushQuad(v, idx, x0, y0 + t, x0 + t, y1 - t, 0, 0, 0, 0, c);  // left
    pushQuad(v, idx, x1 - t, y0 + t, x1, y1 - t, 0, 0, 0, 0, c);  // right
}

/// Open or extend a `SolidColor` draw at the current end of `indices_`.
/// Each scissor change forces a new draw call.
inline void ensureSolidDraw(std::vector<VertexBackend::VertexDraw>& draws,
                            std::size_t indicesSize, Rect scissor) noexcept {
    if (!draws.empty() &&
        draws.back().kind == VertexDrawKind::SolidColor &&
        draws.back().scissor == scissor) {
        return;  // will keep extending the existing draw via indexCount
    }
    VertexBackend::VertexDraw d{};
    d.firstIndex = static_cast<std::uint32_t>(indicesSize);
    d.indexCount = 0;
    d.scissor = scissor;
    d.kind = VertexDrawKind::SolidColor;
    draws.push_back(d);
}

} // namespace

void VertexBackend::reserve(std::size_t expectedVertices, std::size_t expectedDraws) {
    vertices_.reserve(expectedVertices);
    indices_.reserve(expectedVertices * 3 / 2);
    draws_.reserve(expectedDraws);
}

void VertexBackend::submit(const DrawList& list) {
    clear();
    if (vertices_.capacity() < kInitialVertices) vertices_.reserve(kInitialVertices);
    if (indices_.capacity() < kInitialIndices)   indices_.reserve(kInitialIndices);
    if (draws_.capacity() < kInitialDraws)       draws_.reserve(kInitialDraws);

    // Clip stack (last entry = active scissor).
    Rect clipStack[16];
    std::size_t clipDepth = 0;
    auto activeScissor = [&]() -> Rect {
        return clipDepth == 0 ? Rect{} : clipStack[clipDepth - 1];
    };

    for (const auto& cmd : list.commands()) {
        switch (cmd.kind) {
        case DrawCmdKind::ClipPush: {
            if (clipDepth < 16) clipStack[clipDepth++] = cmd.payload.clip.bounds;
            break;
        }
        case DrawCmdKind::ClipPop: {
            if (clipDepth > 0) --clipDepth;
            break;
        }
        case DrawCmdKind::Rect: {
            const auto& r = cmd.payload.rect;
            ensureSolidDraw(draws_, indices_.size(), activeScissor());
            const std::size_t indexBefore = indices_.size();
            if (r.thickness <= 0) {
                pushQuad(vertices_, indices_,
                         static_cast<float>(r.bounds.x),
                         static_cast<float>(r.bounds.y),
                         static_cast<float>(r.bounds.x + r.bounds.w),
                         static_cast<float>(r.bounds.y + r.bounds.h),
                         0, 0, 0, 0, r.color);
            } else {
                pushOutlineRect(vertices_, indices_, r.bounds, r.thickness, r.color);
            }
            const std::size_t added = indices_.size() - indexBefore;
            draws_.back().indexCount += static_cast<std::uint32_t>(added);
            break;
        }
        case DrawCmdKind::Line: {
            const auto& ln = cmd.payload.line;
            ensureSolidDraw(draws_, indices_.size(), activeScissor());
            const std::size_t indexBefore = indices_.size();
            // Render as a thin axis-aligned rect spanning [a, b]. Diagonal
            // lines come out as bounding-box rects in v1.0 — full quad
            // generation lands in v1.x.
            const std::int32_t x0 = ln.a.x < ln.b.x ? ln.a.x : ln.b.x;
            const std::int32_t y0 = ln.a.y < ln.b.y ? ln.a.y : ln.b.y;
            const std::int32_t x1 = ln.a.x > ln.b.x ? ln.a.x : ln.b.x;
            const std::int32_t y1 = ln.a.y > ln.b.y ? ln.a.y : ln.b.y;
            const std::int32_t t = ln.thickness > 0 ? ln.thickness : 1;
            pushQuad(vertices_, indices_,
                     static_cast<float>(x0), static_cast<float>(y0),
                     static_cast<float>(x1 + t), static_cast<float>(y1 + t),
                     0, 0, 0, 0, ln.color);
            const std::size_t added = indices_.size() - indexBefore;
            draws_.back().indexCount += static_cast<std::uint32_t>(added);
            break;
        }
        case DrawCmdKind::Text: {
            const auto& t = cmd.payload.text;
            // FontAtlas draws use their own draw call (host binds the
            // atlas texture); we put a placeholder quad per character at
            // 7 px advance.
            VertexDraw d{};
            d.firstIndex = static_cast<std::uint32_t>(indices_.size());
            d.scissor = activeScissor();
            d.kind = VertexDrawKind::FontAtlas;
            draws_.push_back(d);
            const std::size_t indexBefore = indices_.size();
            for (std::uint32_t i = 0; i < t.textLength; ++i) {
                const float x0 = static_cast<float>(t.pos.x + static_cast<std::int32_t>(i) * 7);
                const float y0 = static_cast<float>(t.pos.y);
                pushQuad(vertices_, indices_,
                         x0, y0, x0 + 6.0f, y0 + 12.0f,
                         0, 0, 1, 1, t.color);
            }
            const std::size_t added = indices_.size() - indexBefore;
            draws_.back().indexCount += static_cast<std::uint32_t>(added);
            break;
        }
        case DrawCmdKind::Image: {
            const auto& im = cmd.payload.image;
            VertexDraw d{};
            d.firstIndex = static_cast<std::uint32_t>(indices_.size());
            d.scissor = activeScissor();
            d.kind = VertexDrawKind::Image;
            d.imageHandle = im.imageHandle;
            draws_.push_back(d);
            const std::size_t indexBefore = indices_.size();
            pushQuad(vertices_, indices_,
                     static_cast<float>(im.bounds.x),
                     static_cast<float>(im.bounds.y),
                     static_cast<float>(im.bounds.x + im.bounds.w),
                     static_cast<float>(im.bounds.y + im.bounds.h),
                     0, 0, 1, 1, im.tint);
            const std::size_t added = indices_.size() - indexBefore;
            draws_.back().indexCount += static_cast<std::uint32_t>(added);
            break;
        }
        }
    }
}

} // namespace threadmaxx::ui
