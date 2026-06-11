#pragma once

/// @file detail/id_stack.hpp
/// @brief Per-frame widget ID stack. Identity is the FNV-1a-64 hash of every
/// segment pushed onto the stack. Stable across frames as long as the caller
/// pushes the same segments in the same order.
///
/// Not part of the public API surface — `context.hpp` re-exports the
/// `pushId` / `popId` / `currentId` operations.

#include <array>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string_view>

#include "threadmaxx_ui/config.hpp"
#include "threadmaxx_ui/types.hpp"

namespace threadmaxx::ui::detail {

/// FNV-1a-64 constants — frozen so the hash result is deterministic across
/// platforms.
inline constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
inline constexpr std::uint64_t kFnvPrime       = 0x00000100000001b3ULL;

/// One-pass FNV-1a-64 over a byte range. `seed` is the previous hash so the
/// id stack can mix segments without re-hashing the whole path.
[[nodiscard]] inline constexpr std::uint64_t fnv1a64(const void* data,
                                                    std::size_t bytes,
                                                    std::uint64_t seed = kFnvOffsetBasis) noexcept {
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < bytes; ++i) {
        h ^= static_cast<std::uint64_t>(p[i]);
        h *= kFnvPrime;
    }
    return h;
}

/// Convenience overload for string views.
[[nodiscard]] inline constexpr std::uint64_t fnv1a64(std::string_view s,
                                                    std::uint64_t seed = kFnvOffsetBasis) noexcept {
    std::uint64_t h = seed;
    for (char c : s) {
        h ^= static_cast<std::uint64_t>(static_cast<std::uint8_t>(c));
        h *= kFnvPrime;
    }
    return h;
}

/// Fixed-capacity stack of accumulated hashes. The top entry is the current
/// widget ID; pushing a segment hashes it into the top and pushes a new
/// frame; popping reverts.
class IdStack {
public:
    IdStack() noexcept {
        slots_[0] = kFnvOffsetBasis;
        depth_ = 1;
    }

    void reset() noexcept {
        slots_[0] = kFnvOffsetBasis;
        depth_ = 1;
    }

    /// Push a string segment. Returns the new top hash.
    std::uint64_t pushString(std::string_view s) noexcept {
        const std::uint64_t parent = top();
        const std::uint64_t child  = fnv1a64(s, parent);
        return pushHash(child);
    }

    /// Push a 64-bit integer segment (eg. per-row index, entity handle).
    std::uint64_t pushInt(std::uint64_t v) noexcept {
        const std::uint64_t parent = top();
        const std::uint64_t child  = fnv1a64(&v, sizeof(v), parent);
        return pushHash(child);
    }

    /// Push an already-hashed value (eg. precomputed `WidgetID::value`).
    std::uint64_t pushHash(std::uint64_t h) noexcept {
        if (depth_ < slots_.size()) {
            slots_[depth_++] = h;
        } else {
            assert(false && "IdStack overflow — increase kIdStackDepth");
        }
        return h;
    }

    /// Pop the top entry. Safe to call when empty (no-op, returns base).
    std::uint64_t pop() noexcept {
        if (depth_ > 1) --depth_;
        return top();
    }

    [[nodiscard]] std::uint64_t top() const noexcept {
        return slots_[depth_ - 1];
    }

    [[nodiscard]] WidgetID currentId() const noexcept {
        return WidgetID{top()};
    }

    [[nodiscard]] std::size_t depth() const noexcept { return depth_; }

private:
    std::array<std::uint64_t, kIdStackDepth> slots_{};
    std::size_t depth_ = 1;
};

} // namespace threadmaxx::ui::detail
