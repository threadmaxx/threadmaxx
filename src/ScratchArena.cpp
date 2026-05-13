/// @file ScratchArena.cpp
/// Chained-slab bump allocator. `reset()` rewinds to slabs_[0], offset 0,
/// keeping the underlying buffers for reuse; if a job overshoots the
/// existing capacity we push a new slab at ≥2× the previous size. The
/// chained design means an allocation never invalidates pointers from
/// earlier allocations in the same epoch (only `reset()` does).
#include "threadmaxx/ScratchArena.hpp"

#include <algorithm>
#include <cstddef>

namespace threadmaxx {

namespace {
constexpr std::size_t kDefaultSlabBytes = 4096;
} // namespace

ScratchArena::ScratchArena(std::size_t initialBytes) {
    if (initialBytes > 0) {
        pushSlab(initialBytes);
    }
}

void ScratchArena::pushSlab(std::size_t minBytes) {
    Slab s;
    s.capacity = std::max(minBytes, kDefaultSlabBytes);
    s.bytes = std::make_unique<std::byte[]>(s.capacity);
    slabs_.push_back(std::move(s));
}

void* ScratchArena::allocateBytes(std::size_t size, std::size_t align) {
    if (slabs_.empty()) {
        pushSlab(std::max(size + align, kDefaultSlabBytes));
        slab_   = 0;
        offset_ = 0;
    }

    for (;;) {
        Slab& s = slabs_[slab_];
        // Align the bump position up to `align`.
        const std::size_t aligned = (offset_ + align - 1u) & ~(align - 1u);
        if (aligned + size <= s.capacity) {
            std::byte* p = s.bytes.get() + aligned;
            offset_ = aligned + size;
            return static_cast<void*>(p);
        }
        // Current slab is full. Try the next existing one (after a prior
        // reset+grow we may already have spare slabs in the chain).
        if (slab_ + 1 < slabs_.size()) {
            slab_++;
            offset_ = 0;
            continue;
        }
        // No spare — grow by at least 2x the largest existing slab so
        // amortized growth stays O(log n).
        const std::size_t lastCap = slabs_.back().capacity;
        pushSlab(std::max(size + align, lastCap * 2u));
        slab_   = slabs_.size() - 1u;
        offset_ = 0;
    }
}

void ScratchArena::reset() noexcept {
    slab_   = 0;
    offset_ = 0;
}

std::size_t ScratchArena::bytesUsed() const noexcept {
    if (slabs_.empty()) return 0;
    std::size_t total = offset_;
    for (std::size_t i = 0; i < slab_; ++i) total += slabs_[i].capacity;
    return total;
}

std::size_t ScratchArena::capacity() const noexcept {
    std::size_t total = 0;
    for (const auto& s : slabs_) total += s.capacity;
    return total;
}

} // namespace threadmaxx
