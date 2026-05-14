#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace threadmaxx {

/// Header-only frame-to-frame bump allocator backing per-frame upload
/// staging. Renderer-neutral — works for Vulkan (one VkBuffer-backed
/// staging arena per in-flight frame), D3D12 (UPLOAD heap), WebGPU
/// (mapped buffer), or a pure-software backend.
///
/// The ring holds `frameCount` independent slabs ("frames"). Each tick
/// the renderer calls @ref nextFrame to advance to the next slab and
/// reset its write head; subsequent @ref allocate calls bump within
/// that slab. The previous frame's data is still readable from the
/// renderer's other API (e.g. GPU is mid-flight on it).
///
/// Not thread-safe; intended to be used from a single render thread.
/// If a renderer wants parallel uploads, use one @ref UploadRing per
/// worker thread.
class UploadRing {
public:
    /// @param frameCount Number of independent slabs to maintain. Two
    ///        is the typical double-buffered renderer; three suits a
    ///        triple-buffered Vulkan swapchain.
    /// @param bytesPerFrame Size of each slab. Allocations exceeding
    ///        this either bail (returning a null span) or grow the
    ///        slab — configurable via @ref setGrowOnOverflow.
    explicit UploadRing(std::uint32_t frameCount = 2,
                        std::size_t bytesPerFrame = 1u << 20) noexcept
        : frames_(frameCount), growOnOverflow_(false) {
        for (auto& f : frames_) {
            f.storage.resize(bytesPerFrame);
        }
    }

    /// Advance to the next slab and reset its write head. Call once per
    /// rendered frame, before any @ref allocate calls for that frame.
    void nextFrame() noexcept {
        current_ = (current_ + 1u) % static_cast<std::uint32_t>(frames_.size());
        frames_[current_].head = 0;
    }

    /// Reserve @p bytes from the current slab and return a writable
    /// pointer. If @p alignment is supplied (defaults to 16, suitable
    /// for vec4-aligned uniform blocks), the head is rounded up before
    /// the allocation.
    ///
    /// Returns nullptr if @p bytes does not fit and @ref
    /// setGrowOnOverflow is `false`. If `true`, the slab is grown to at
    /// least `head + bytes` and the call succeeds.
    void* allocate(std::size_t bytes, std::size_t alignment = 16) noexcept {
        if (frames_.empty() || bytes == 0) return nullptr;
        auto& f = frames_[current_];
        const std::size_t aligned = (f.head + alignment - 1) & ~(alignment - 1);
        if (aligned + bytes > f.storage.size()) {
            if (!growOnOverflow_) return nullptr;
            f.storage.resize(aligned + bytes);
        }
        f.head = aligned + bytes;
        return f.storage.data() + aligned;
    }

    /// Copy @p src into the current slab and return the destination
    /// offset (renderer maps this to a GPU buffer offset). Returns
    /// `SIZE_MAX` on out-of-space if growth is disabled.
    std::size_t pushBytes(const void* src, std::size_t bytes,
                          std::size_t alignment = 16) noexcept {
        void* dst = allocate(bytes, alignment);
        if (!dst) return static_cast<std::size_t>(-1);
        std::memcpy(dst, src, bytes);
        const auto& f = frames_[current_];
        return f.head - bytes;
    }

    /// Current write head into the active slab (bytes). Useful for the
    /// renderer to record a flush-range.
    std::size_t head() const noexcept {
        return frames_.empty() ? 0 : frames_[current_].head;
    }

    /// Capacity of each slab.
    std::size_t bytesPerFrame() const noexcept {
        return frames_.empty() ? 0 : frames_[current_].storage.size();
    }

    /// Number of slabs (`frameCount` from construction).
    std::uint32_t frameCount() const noexcept {
        return static_cast<std::uint32_t>(frames_.size());
    }

    /// Index of the active slab (0-based, advances every @ref nextFrame).
    std::uint32_t currentFrame() const noexcept { return current_; }

    /// When true, an oversized allocation grows the current slab. When
    /// false (the default), the allocation fails and returns nullptr.
    void setGrowOnOverflow(bool grow) noexcept { growOnOverflow_ = grow; }

private:
    struct FrameSlab {
        std::vector<std::uint8_t> storage;
        std::size_t head = 0;
    };
    std::vector<FrameSlab> frames_;
    std::uint32_t          current_ = 0;
    bool                   growOnOverflow_;
};

} // namespace threadmaxx
