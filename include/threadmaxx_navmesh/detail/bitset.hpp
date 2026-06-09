#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

/// Internal bitset used by walkers and the (future) A* search to track
/// "visited polygons" without per-step allocation. Header-only so the
/// detail can move freely between batches.
///
/// Owned by the `detail/` namespace because nothing outside the library
/// should reach for this — A* state, walker scratch buffers and the
/// crowd solver all reuse instances across ticks via `clear()` which
/// preserves capacity.
namespace threadmaxx::navmesh::detail {

class Bitset {
public:
    Bitset() = default;
    explicit Bitset(std::size_t bits) { resize(bits); }

    void resize(std::size_t bits) {
        words_.assign((bits + 63) / 64, 0);
        size_ = bits;
    }

    /// Re-zero every bit while preserving the underlying allocation.
    void clear() noexcept {
        for (auto& w : words_) w = 0;
    }

    void set(std::size_t i) noexcept {
        words_[i >> 6] |= (std::uint64_t{1} << (i & 63));
    }

    bool test(std::size_t i) const noexcept {
        return (words_[i >> 6] >> (i & 63)) & std::uint64_t{1};
    }

    /// Atomic-style `test-then-set`. Returns the previous value so the
    /// caller can branch on "first observation" without a separate
    /// `test()` step.
    bool testAndSet(std::size_t i) noexcept {
        const std::uint64_t mask = std::uint64_t{1} << (i & 63);
        const std::uint64_t prev = words_[i >> 6];
        words_[i >> 6] = prev | mask;
        return (prev & mask) != 0;
    }

    std::size_t size() const noexcept { return size_; }

private:
    std::vector<std::uint64_t> words_;
    std::size_t size_{};
};

} // namespace threadmaxx::navmesh::detail
