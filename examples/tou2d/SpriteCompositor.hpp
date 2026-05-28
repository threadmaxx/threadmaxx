#pragma once

// M4.8 — per-tick sprite compositor. Holds the foreground RGBA buffer
// covering the level extent (same pixel grid the background uses),
// blits each ship's current rotation frame onto it, and tracks a
// dirty bbox so the host can upload only what changed.
//
// Owned by `main.cpp` alongside the BackgroundPainter; not an
// `ISystem` — invoked once per tick after `engine.step()` from the
// sim thread.

#include "DemoTypes.hpp"
#include "SpriteAtlas.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Handles.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace tou2d {

class SpriteCompositor {
public:
    SpriteCompositor() = default;

    /// Size the foreground buffer to (imgWidth × imgHeight), pinning
    /// the world-origin pixel at (halfImgW, halfImgH) and the world-
    /// to-pixel scale at `wuPerPx`.
    void resize(std::int32_t imgWidth, std::int32_t imgHeight,
                std::int32_t halfImgW, std::int32_t halfImgH,
                float wuPerPx) {
        imgW_   = imgWidth;
        imgH_   = imgHeight;
        halfX_  = halfImgW;
        halfY_  = halfImgH;
        wuPerPx_ = wuPerPx;
        pixels_.assign(static_cast<std::size_t>(imgW_) *
                       static_cast<std::size_t>(imgH_) * 4u, 0);
        dirty_ = true;
        dirtyMinX_ = 0; dirtyMinY_ = 0;
        dirtyMaxX_ = imgW_; dirtyMaxY_ = imgH_;
        cache_.clear();
    }

    /// Returns the linear atlas index for `atlas`; the compositor
    /// keeps ownership.
    std::int32_t addAtlas(SpriteAtlas&& atlas) {
        if (!atlas.valid()) return -1;
        const std::int32_t idx = static_cast<std::int32_t>(atlases_.size());
        atlases_.emplace_back(std::move(atlas));
        return idx;
    }

    std::span<const std::uint8_t> pixels() const noexcept {
        return std::span<const std::uint8_t>(pixels_.data(), pixels_.size());
    }
    std::int32_t width()  const noexcept { return imgW_; }
    std::int32_t height() const noexcept { return imgH_; }

    /// Composite every visible ship's sprite into the foreground buffer.
    /// Clears each ship's prev-frame bbox before blitting the new
    /// position so stale ghosts don't pile up. Sim-thread only; safe
    /// to call between `engine.step()` invocations.
    void tick(const threadmaxx::World& world, const UserComponentIds& ids);

    /// Read + clear the dirty bbox. Returns false if nothing to upload.
    bool consumeDirty(std::int32_t& outX, std::int32_t& outY,
                     std::int32_t& outW, std::int32_t& outH) noexcept {
        if (!dirty_) return false;
        outX = dirtyMinX_;
        outY = dirtyMinY_;
        outW = dirtyMaxX_ - dirtyMinX_;
        outH = dirtyMaxY_ - dirtyMinY_;
        dirty_ = false;
        return outW > 0 && outH > 0;
    }

    /// Copy a rect of pixels into `out` contiguously (row-major RGBA).
    void copyRegion(std::int32_t x, std::int32_t y,
                    std::int32_t w, std::int32_t h,
                    std::vector<std::uint8_t>& out) const {
        out.resize(static_cast<std::size_t>(w) *
                   static_cast<std::size_t>(h) * 4u);
        for (std::int32_t r = 0; r < h; ++r) {
            const std::size_t srcOff =
                (static_cast<std::size_t>(y + r) *
                 static_cast<std::size_t>(imgW_) +
                 static_cast<std::size_t>(x)) * 4u;
            std::memcpy(out.data() + static_cast<std::size_t>(r) *
                                       static_cast<std::size_t>(w) * 4u,
                        pixels_.data() + srcOff,
                        static_cast<std::size_t>(w) * 4u);
        }
    }

private:
    struct PrevBlit {
        std::int32_t px = 0;
        std::int32_t py = 0;
        std::int32_t atlasIdx = -1;
        std::uint8_t frame = 0;
        bool         present = false;
    };

    static std::uint32_t frameForAngle(float angleZ) noexcept {
        constexpr float kPi    = 3.14159265358979323846f;
        constexpr float kTwoPi = 6.28318530717958647692f;
        constexpr float kStep  = kTwoPi /
            static_cast<float>(shp::kBodyRotationCount);
        float u = angleZ - kPi;                  // frame 0 at θ = π (facing down)
        u -= kTwoPi * std::floor(u / kTwoPi);
        int frame = static_cast<int>(std::floor(u / kStep + 0.5f));
        frame %= static_cast<int>(shp::kBodyRotationCount);
        if (frame < 0) frame += static_cast<int>(shp::kBodyRotationCount);
        return static_cast<std::uint32_t>(frame);
    }

    static float orientationAngleZ(const threadmaxx::Quat& q) noexcept {
        return std::atan2(2.0f * (q.w * q.z + q.x * q.y),
                          1.0f - 2.0f * (q.y * q.y + q.z * q.z));
    }

    void worldToPx(float wx, float wy,
                   std::int32_t& outPx, std::int32_t& outPy) const noexcept {
        const float invWu = wuPerPx_ > 0.0f ? 1.0f / wuPerPx_ : 1.0f;
        outPx = static_cast<std::int32_t>(std::round(wx * invWu)) + halfX_;
        outPy = halfY_ - static_cast<std::int32_t>(std::round(wy * invWu));
    }

    void unionDirty(std::int32_t x0, std::int32_t y0,
                    std::int32_t x1, std::int32_t y1) noexcept {
        x0 = std::clamp(x0, 0, imgW_);
        y0 = std::clamp(y0, 0, imgH_);
        x1 = std::clamp(x1, 0, imgW_);
        y1 = std::clamp(y1, 0, imgH_);
        if (x0 >= x1 || y0 >= y1) return;
        if (!dirty_) {
            dirty_ = true;
            dirtyMinX_ = x0; dirtyMinY_ = y0;
            dirtyMaxX_ = x1; dirtyMaxY_ = y1;
        } else {
            dirtyMinX_ = std::min(dirtyMinX_, x0);
            dirtyMinY_ = std::min(dirtyMinY_, y0);
            dirtyMaxX_ = std::max(dirtyMaxX_, x1);
            dirtyMaxY_ = std::max(dirtyMaxY_, y1);
        }
    }

    void clearRect(std::int32_t x0, std::int32_t y0,
                   std::int32_t x1, std::int32_t y1) noexcept {
        x0 = std::clamp(x0, 0, imgW_);
        y0 = std::clamp(y0, 0, imgH_);
        x1 = std::clamp(x1, 0, imgW_);
        y1 = std::clamp(y1, 0, imgH_);
        for (std::int32_t y = y0; y < y1; ++y) {
            std::uint8_t* row = pixels_.data() +
                (static_cast<std::size_t>(y) *
                 static_cast<std::size_t>(imgW_) +
                 static_cast<std::size_t>(x0)) * 4u;
            std::memset(row, 0,
                        static_cast<std::size_t>(x1 - x0) * 4u);
        }
    }

    void blitFrame(const SpriteAtlas& atlas, std::uint32_t frameIdx,
                   std::int32_t cx, std::int32_t cy) noexcept {
        const std::int32_t w = atlas.frameWidth;
        const std::int32_t h = atlas.frameHeight;
        const std::int32_t x0 = cx - w / 2;
        const std::int32_t y0 = cy - h / 2;
        const std::uint8_t* src = atlas.frames[frameIdx].data();
        for (std::int32_t r = 0; r < h; ++r) {
            const std::int32_t dy = y0 + r;
            if (dy < 0 || dy >= imgH_) continue;
            for (std::int32_t c = 0; c < w; ++c) {
                const std::int32_t dx = x0 + c;
                if (dx < 0 || dx >= imgW_) continue;
                const std::size_t srcOff =
                    (static_cast<std::size_t>(r) *
                     static_cast<std::size_t>(w) +
                     static_cast<std::size_t>(c)) * 4u;
                if (src[srcOff + 3] == 0) continue;
                const std::size_t dstOff =
                    (static_cast<std::size_t>(dy) *
                     static_cast<std::size_t>(imgW_) +
                     static_cast<std::size_t>(dx)) * 4u;
                pixels_[dstOff + 0] = src[srcOff + 0];
                pixels_[dstOff + 1] = src[srcOff + 1];
                pixels_[dstOff + 2] = src[srcOff + 2];
                pixels_[dstOff + 3] = src[srcOff + 3];
            }
        }
    }

    std::vector<std::uint8_t>     pixels_;
    std::vector<SpriteAtlas>      atlases_;
    /// Keyed by `EntityHandle::index` — the per-entity prev-blit cache
    /// the compositor uses to clear stale pixels before blitting the
    /// new rotation. Stale entries (entity destroyed, never revisited)
    /// don't hurt — they sit untouched until the entity index is
    /// reused, at which point the new ship overwrites the cache entry.
    std::unordered_map<std::uint32_t, PrevBlit> cache_;
    std::int32_t                  imgW_  = 0;
    std::int32_t                  imgH_  = 0;
    std::int32_t                  halfX_ = 0;
    std::int32_t                  halfY_ = 0;
    float                         wuPerPx_ = 1.0f;
    bool                          dirty_ = false;
    std::int32_t                  dirtyMinX_ = 0;
    std::int32_t                  dirtyMinY_ = 0;
    std::int32_t                  dirtyMaxX_ = 0;
    std::int32_t                  dirtyMaxY_ = 0;
};

inline void SpriteCompositor::tick(const threadmaxx::World& world,
                                    const UserComponentIds& ids) {
    if (atlases_.empty() || imgW_ == 0 || imgH_ == 0) return;
    if (!ids.sprite.valid()) return;

    const threadmaxx::ComponentSet required{
        threadmaxx::Component::Transform,
    };

    // 2026-05-28 — three-pass redesign per M5.2 fixes:
    //   * Pass A: clear EVERY entity's prev-blit rect, mark each cache
    //     entry "not seen this tick" so we can detect dead/destroyed
    //     ships at the end and drop them. Clearing first means the
    //     blits in Pass B overwrite freshly-cleared pixels — no
    //     overlap-leaves-a-black-patch artifact when two ship
    //     bounding boxes cross.
    //   * Pass B: blit every visible ship; mark its cache entry
    //     "seen." Visible = has ShipSpriteRef AND not disabled.
    //   * Pass C: any cache entry still unseen (ship died, was
    //     destroyed, or migrated out of the sprite archetype) is
    //     erased — its prev-blit rect was already cleared in Pass A
    //     so the sprite vanishes the same tick the ship dies.
    //
    // Cost: one extra walk of cache_ each tick (~67 entries max).

    // Pass A — clear every cached prev rect.
    for (auto& [key, prev] : cache_) {
        if (!prev.present) continue;
        if (prev.atlasIdx < 0 ||
            prev.atlasIdx >= static_cast<std::int32_t>(atlases_.size())) {
            continue;
        }
        const SpriteAtlas& atlas = atlases_[static_cast<std::size_t>(prev.atlasIdx)];
        const std::int32_t w = atlas.frameWidth;
        const std::int32_t h = atlas.frameHeight;
        const std::int32_t lx0 = prev.px - w / 2;
        const std::int32_t ly0 = prev.py - h / 2;
        clearRect(lx0, ly0, lx0 + w, ly0 + h);
        unionDirty(lx0, ly0, lx0 + w, ly0 + h);
        prev.present = false;  // re-set true in Pass B for surviving ships
    }

    // Pass B — blit every still-alive ship.
    world.forEachChunkOf(required, [&](const threadmaxx::internal::ArchetypeChunk& chunk) {
        if (!chunk.mask.has(ids.sprite.componentBit()))         return;
        if (chunk.mask.has(threadmaxx::Component::DisabledTag)) return;

        const auto& entities   = chunk.entities;
        const auto& transforms = chunk.transforms;
        const auto refSpan = threadmaxx::user::chunkSpan<ShipSpriteRef>(chunk, ids.sprite);
        const std::size_t n = entities.size();
        for (std::size_t row = 0; row < n; ++row) {
            const ShipSpriteRef& ref = refSpan[row];
            if (ref.atlasIdx < 0 ||
                ref.atlasIdx >= static_cast<std::int32_t>(atlases_.size())) {
                continue;
            }
            const SpriteAtlas& atlas = atlases_[static_cast<std::size_t>(ref.atlasIdx)];
            if (!atlas.valid()) continue;

            const float ang = orientationAngleZ(transforms[row].orientation);
            const std::uint32_t frameIdx = frameForAngle(ang);

            std::int32_t px = 0, py = 0;
            worldToPx(transforms[row].position.x,
                      transforms[row].position.y, px, py);

            const std::int32_t w = atlas.frameWidth;
            const std::int32_t h = atlas.frameHeight;
            const std::uint32_t key = entities[row].index;

            blitFrame(atlas, frameIdx, px, py);
            const std::int32_t nx0 = px - w / 2;
            const std::int32_t ny0 = py - h / 2;
            unionDirty(nx0, ny0, nx0 + w, ny0 + h);

            PrevBlit& prev = cache_[key];
            prev.px       = px;
            prev.py       = py;
            prev.atlasIdx = ref.atlasIdx;
            prev.frame    = static_cast<std::uint8_t>(frameIdx);
            prev.present  = true;
        }
    });

    // Pass C — drop cache entries we didn't see this tick. The prev
    // rect was already cleared in Pass A, so simply forgetting the
    // key is enough.
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (!it->second.present) it = cache_.erase(it);
        else                     ++it;
    }
}

} // namespace tou2d
