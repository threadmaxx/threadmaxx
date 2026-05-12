#pragma once

#include <cstdint>
#include <functional>

namespace threadmaxx {

// Opaque, generation-tagged entity handle. The 32-bit index points into dense
// storage; the 32-bit generation lets us detect use-after-destroy.
struct EntityHandle {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    constexpr bool valid() const noexcept { return generation != 0; }

    constexpr bool operator==(const EntityHandle& other) const noexcept {
        return index == other.index && generation == other.generation;
    }
    constexpr bool operator!=(const EntityHandle& other) const noexcept {
        return !(*this == other);
    }
};

inline constexpr EntityHandle kInvalidEntity{};

} // namespace threadmaxx

namespace std {
template <>
struct hash<threadmaxx::EntityHandle> {
    std::size_t operator()(const threadmaxx::EntityHandle& h) const noexcept {
        return (static_cast<std::size_t>(h.generation) << 32) ^ h.index;
    }
};
} // namespace std
