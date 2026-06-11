#pragma once

#include <array>
#include <cstdint>

#include "threadmaxx_input/types.hpp"

namespace threadmaxx::input::detail {

// Fixed-size bitset over Key. Stored as 64-bit words for cheap XOR diffs in
// edge detection.
struct KeyBitset {
    std::array<std::uint64_t, kKeyBitsetWords> words{};

    constexpr void clear() noexcept {
        for (auto& w : words) w = 0;
    }

    constexpr bool test(Key k) const noexcept {
        const auto i = static_cast<std::uint16_t>(k);
        if (i >= static_cast<std::uint16_t>(Key::Count)) return false;
        return (words[i >> 6] >> (i & 63u)) & 1u;
    }

    constexpr void set(Key k, bool value) noexcept {
        const auto i = static_cast<std::uint16_t>(k);
        if (i >= static_cast<std::uint16_t>(Key::Count)) return;
        const std::uint64_t bit = std::uint64_t{1} << (i & 63u);
        if (value) {
            words[i >> 6] |= bit;
        } else {
            words[i >> 6] &= ~bit;
        }
    }

    // Returns the bits that transitioned 0→1 between `previous` and `current`.
    static constexpr KeyBitset transitionsToHigh(const KeyBitset& previous,
                                                 const KeyBitset& current) noexcept {
        KeyBitset out{};
        for (std::size_t i = 0; i < out.words.size(); ++i) {
            out.words[i] = current.words[i] & ~previous.words[i];
        }
        return out;
    }

    // Returns the bits that transitioned 1→0 between `previous` and `current`.
    static constexpr KeyBitset transitionsToLow(const KeyBitset& previous,
                                                const KeyBitset& current) noexcept {
        KeyBitset out{};
        for (std::size_t i = 0; i < out.words.size(); ++i) {
            out.words[i] = previous.words[i] & ~current.words[i];
        }
        return out;
    }
};

}  // namespace threadmaxx::input::detail
