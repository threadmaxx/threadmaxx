#pragma once

#include "threadmaxx/UserComponent.hpp"

#include <cstdint>
#include <mutex>
#include <typeindex>
#include <vector>

namespace threadmaxx::internal {

/// Engine-owned mapping of user-registered POD types to
/// @ref UserComponentId tokens (bit + stride + type_index). Bits are
/// allocated from 16 upward — built-ins occupy bits 0..15.
///
/// Thread-safety: registration is locked under an internal mutex (it's
/// cheap and called rarely, typically during setup). Read-only queries
/// (@ref find, @ref strideFor) take the same lock; in steady state the
/// only readers are the @ref ArchetypeTable when materializing a new
/// chunk's user columns and the engine's commit path looking up
/// strides — both are sim-thread-only.
class UserComponentRegistry {
public:
    /// Register a type by `type_index` + stride. Idempotent: re-
    /// registering the same `typeid(T)` returns the existing
    /// @ref UserComponentId; the stride must match. Bit assignment is
    /// registration-order stable.
    ///
    /// @returns A valid @ref UserComponentId (bit ≥ 16). Returns an
    ///          invalid id if the registry has exhausted user-bit space
    ///          (currently 48 bits available — 16..63).
    UserComponentId reg(std::type_index type, std::uint32_t stride) noexcept;

    /// Look up an existing registration by `typeid`. Returns an invalid
    /// id when absent.
    UserComponentId find(std::type_index type) const noexcept;

    /// Look up the stride for the given user-component bit, or 0 if no
    /// such registration exists. Hot path: called once per user-column
    /// instantiation in @ref ArchetypeTable::getOrCreateArchetype.
    std::uint32_t strideFor(std::uint32_t bit) const noexcept;

    /// Number of currently-registered user component types.
    std::size_t size() const noexcept;

private:
    struct Entry {
        std::type_index type;
        std::uint32_t   bit;
        std::uint32_t   stride;
    };
    mutable std::mutex mtx_;
    std::vector<Entry> entries_;
    // Next free bit. Built-ins occupy 0..15.
    std::uint32_t      nextBit_ = 16;
};

} // namespace threadmaxx::internal
