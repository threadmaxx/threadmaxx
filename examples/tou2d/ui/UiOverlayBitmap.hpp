#pragma once

// tou2d::ui — M6.0b CPU-side UI overlay framebuffer.
//
// A plain RGBA8 buffer the game's `UISystem`-and-friends paint into
// each tick; the renderer's `setUiOverlayFromRgba` /
// `updateUiOverlayRegion` then uploads the dirty bbox to the GPU
// texture sampled by the screen-space overlay quad (M6.0b's Vulkan
// path). Independent of the font/text-layout split — the compositor
// owns blitting; the layout math still lives in `TextPrintf.hpp`.
//
// Color encoding: packed `0xAABBGGRR` — same convention used by
// DebugLine, DebugPoint, and `tou2d::ui::TextGlyphQuad::color`. On
// little-endian (x86_64) the packed bytes match RGBA8 (R, G, B, A
// order) so no permute is needed before handing the buffer to
// `TextureLoader::createFromRgba`.
//
// Dirty-bbox tracking: every paint method extends a per-bitmap
// dirty rect; `consumeDirty()` returns and resets it so the renderer
// uploads only the touched region per tick.
//
// Thread safety: not safe for concurrent mutation. Build the bitmap
// on the sim thread (e.g. inside `postStep`), upload from the same
// thread before the next `step()`.

#include "Font.hpp"
#include "TextPrintf.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace tou2d::ui {

/// Rectangular subregion of a UiOverlayBitmap. Coordinates in
/// PIXELS; `(x, y)` is the top-left corner; `w`/`h` are the
/// inclusive extent. Empty when `w == 0 || h == 0`.
struct DirtyRect {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    [[nodiscard]] bool empty() const noexcept { return w == 0 || h == 0; }
};

class UiOverlayBitmap {
public:
    UiOverlayBitmap() = default;

    /// (Re)allocate to the given pixel dimensions. Clears to fully
    /// transparent. Marks the entire bitmap dirty on first call so
    /// the first upload pushes the whole frame.
    void resize(std::uint32_t width, std::uint32_t height);

    /// Fill the whole bitmap with the given packed `0xAABBGGRR`. The
    /// most common usage is `clear(0)` (fully transparent) before
    /// re-emitting the UI for a fresh tick.
    void clear(std::uint32_t rgba = 0) noexcept;

    /// Fill a subregion. Clipped to the bitmap; OOB args are silent
    /// no-ops. Marks the painted rect dirty.
    void clearRect(std::int32_t x, std::int32_t y,
                   std::uint32_t w, std::uint32_t h,
                   std::uint32_t rgba) noexcept;

    /// Composite an 8-bit alpha glyph onto the bitmap at (`dstX`,
    /// `dstY`) using `tintRgba` as the foreground color. The source
    /// glyph is `srcW`×`srcH` pixels of R8 alpha laid out in
    /// `srcStride`-byte rows starting at `srcAlpha` (i.e. the
    /// atlas-relative slice for one glyph). Clipped to the bitmap;
    /// pixels outside are silently dropped. Source format mirrors the
    /// `FontAtlas::pixels` R8 buffer. Marks the touched rect dirty.
    void blitGlyphR8Alpha(const std::byte* srcAlpha,
                          std::uint32_t srcW, std::uint32_t srcH,
                          std::uint32_t srcStride,
                          std::int32_t dstX, std::int32_t dstY,
                          std::uint32_t tintRgba) noexcept;

    /// Render `text` at the given baseline using `atlas`. Walks via
    /// `textPrintf` and `blitGlyphR8Alpha`s each visible glyph.
    /// Returns the post-line pen position so callers can chain.
    PenPos drawText(const FontAtlas& atlas,
                    float baseX, float baseY,
                    std::uint32_t color,
                    std::string_view text) noexcept;

    /// Read-only access to the packed bytes for the renderer.
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept {
        return std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(pixels_.data()),
            pixels_.size() * sizeof(std::uint32_t));
    }
    [[nodiscard]] std::uint32_t width()  const noexcept { return width_;  }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

    /// Read-only view of the packed `uint32_t` pixels. Useful for
    /// tests that want to assert specific pixel values.
    [[nodiscard]] std::span<const std::uint32_t> pixels() const noexcept {
        return std::span<const std::uint32_t>(pixels_.data(), pixels_.size());
    }

    /// The current dirty rect — the union of every paint since the
    /// last `consumeDirty`. Doesn't clear; for inspection.
    [[nodiscard]] DirtyRect dirty() const noexcept { return dirty_; }

    /// Returns the current dirty rect AND clears it. The caller
    /// uploads exactly this region; subsequent paints accumulate
    /// fresh dirty bounds for the next tick.
    [[nodiscard]] DirtyRect consumeDirty() noexcept {
        const DirtyRect d = dirty_;
        dirty_ = DirtyRect{};
        return d;
    }

    /// Copy a dirty-rect-shaped slice out of the bitmap into `dst`.
    /// Used by the renderer hot path to feed `updateUiOverlayRegion`
    /// without aliasing the live bitmap pixels. `dst.size()` must be
    /// at least `rect.w * rect.h * 4`. Returns true on success.
    bool extractRegion(const DirtyRect& rect,
                       std::span<std::uint8_t> dst) const noexcept;

private:
    /// Mark a rectangle dirty. Clipped before the union.
    void unionDirty(std::int32_t x, std::int32_t y,
                    std::uint32_t w, std::uint32_t h) noexcept;

    /// Composite a single packed pixel onto an existing packed pixel
    /// using straight alpha. Inlined.
    static std::uint32_t blendOver(std::uint32_t dst, std::uint32_t src) noexcept;

    std::uint32_t              width_  = 0;
    std::uint32_t              height_ = 0;
    std::vector<std::uint32_t> pixels_;
    DirtyRect                  dirty_;
};

}  // namespace tou2d::ui
