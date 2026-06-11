#pragma once

/// @file backends/VertexBackend.hpp
/// @brief Renderer-neutral reference backend. Tessellates the `DrawList`
/// command stream into flat vertex / index / draw-call buffers any GPU
/// host can upload. The host's job:
///
///   1. Call `backend.submit(list)` (engine does this via UIContext::endFrame).
///   2. Upload `backend.vertices()` + `backend.indices()` to GPU buffers.
///   3. Walk `backend.draws()` issuing one draw per `VertexDraw` (binding
///      the texture / atlas, applying the scissor rect).
///
/// Vertex layout is fixed (`VertexBackend::Vertex` is a 20-byte POD).
/// Shader-side: vertex stage expects `position (vec2)` + `uv (vec2)` +
/// `color (rgba8 packed)`; fragment stage samples the bound texture
/// modulated by `color`.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "threadmaxx_ui/backend.hpp"
#include "threadmaxx_ui/types.hpp"

namespace threadmaxx::ui {

class DrawList;

/// Which texture the host should bind for a draw. The UI itself only
/// distinguishes between the font atlas, no texture (solid color), and
/// a host-supplied image; the actual atlas / texture objects are wired
/// at the host side.
enum class VertexDrawKind : std::uint8_t {
    SolidColor = 0,
    FontAtlas  = 1,
    Image      = 2,
};

class VertexBackend final : public IUIBackend {
public:
    /// Single vertex emitted into the GPU stream. 20 B; matches a standard
    /// 2D UI vertex layout.
    struct Vertex {
        float x = 0.0f;
        float y = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        std::uint8_t a = 0;
    };
    static_assert(sizeof(Vertex) == 20);

    /// One draw call. `firstIndex` / `indexCount` index into the indices
    /// buffer; `scissor` is the active clip rect at the time of the draw
    /// (or `Rect{}` meaning "no clip"). `imageHandle` is opaque — the host
    /// dereferences it for `Image` draws.
    struct VertexDraw {
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
        Rect scissor{};
        VertexDrawKind kind = VertexDrawKind::SolidColor;
        std::uint32_t imageHandle = 0;
    };

    void submit(const DrawList& list) override;

    [[nodiscard]] const std::vector<Vertex>&     vertices() const noexcept { return vertices_; }
    [[nodiscard]] const std::vector<std::uint32_t>& indices() const noexcept { return indices_; }
    [[nodiscard]] const std::vector<VertexDraw>& draws() const noexcept { return draws_; }

    /// Resets the staging buffers without releasing capacity. Called at
    /// the top of every `submit()`.
    void clear() noexcept {
        vertices_.clear();
        indices_.clear();
        draws_.clear();
    }

    /// Pre-reserve internal vectors for `expectedVertices` / `expectedDraws`
    /// — hosts that warm up at startup avoid per-frame growth.
    void reserve(std::size_t expectedVertices, std::size_t expectedDraws);

private:
    std::vector<Vertex> vertices_{};
    std::vector<std::uint32_t> indices_{};
    std::vector<VertexDraw> draws_{};
};

} // namespace threadmaxx::ui
