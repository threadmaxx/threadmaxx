#pragma once

#include <cstdint>
#include <functional>

namespace threadmaxx {

/// Opaque, generation-tagged entity handle.
///
/// The 32-bit index points into dense storage; the 32-bit generation
/// lets us detect use-after-destroy. Stale handles never alias new
/// entities because the slot's generation bumps on every destroy.
///
/// A default-constructed handle is invalid (generation == 0); see
/// @ref kInvalidEntity.
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

/// Canonical invalid handle (`generation == 0`).
inline constexpr EntityHandle kInvalidEntity{};

} // namespace threadmaxx

namespace std {
/// Hash specialization so `EntityHandle` works in unordered containers.
template <>
struct hash<threadmaxx::EntityHandle> {
    std::size_t operator()(const threadmaxx::EntityHandle& h) const noexcept {
        return (static_cast<std::size_t>(h.generation) << 32) ^ h.index;
    }
};
} // namespace std
