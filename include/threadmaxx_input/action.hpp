#pragma once

#include <cstdint>
#include <string_view>

namespace threadmaxx::input {

// 32-bit hash of an action name. FNV-1a-64 truncated to the low 32 bits.
// Constexpr so action ids land at compile time and the hashing seed never
// drifts between builds.
using ActionId = std::uint32_t;

constexpr ActionId actionId(std::string_view name) noexcept {
    std::uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a-64 offset basis
    for (char c : name) {
        h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        h *= 0x100000001b3ULL;  // FNV-1a-64 prime
    }
    return static_cast<ActionId>(h & 0xFFFFFFFFu);
}

// Per-frame action evaluation result.
struct ActionTrigger {
    bool held{};      // any bound source is currently held
    bool pressed{};   // any bound source transitioned to held this frame
                      // (suppressed when another bound source was already held)
    bool released{};  // last held source went up this frame
    float value{};    // axis: clamped 0..1; digital: 1.0 while held, 0 otherwise
};

}  // namespace threadmaxx::input
