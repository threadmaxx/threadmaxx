#pragma once

#include "CommandBuffer.hpp"
#include "Components.hpp"
#include "Handles.hpp"
#include "World.hpp"
#include "internal/Archetype.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <typeindex>

namespace threadmaxx {

/// Opaque token for a runtime-registered, game-owned dense-array
/// component (§3.1 batch 6b). Obtained from @ref Engine::registerUserComponent
/// and passed back to @ref addUserComponent / @ref removeUserComponent /
/// @ref user::has / @ref user::tryGet / @ref user::chunkSpan to identify
/// the type. The token is plain-old-data; copying it is free.
///
/// Internally it encodes the @ref ComponentSet bit assigned to the type
/// (always `>= 16`, so user bits never collide with built-ins), the
/// type's stride in bytes (`sizeof(T)`), and a `std::type_index` for the
/// type. Bit assignment is registration-order stable within a process —
/// run the same registration sequence twice and you get the same bits.
struct UserComponentId {
    std::uint32_t  bit    = 0;
    std::uint32_t  stride = 0;
    std::type_index type;

    UserComponentId() noexcept : type(typeid(void)) {}
    UserComponentId(std::uint32_t b, std::uint32_t s, std::type_index t) noexcept
        : bit(b), stride(s), type(t) {}

    /// True iff @c bit is a legal user-component bit (≥16, valid registration).
    bool valid() const noexcept { return bit >= 16 && bit < 64 && stride > 0; }

    /// @c ComponentSet single-bit value for @c bit. Use this anywhere a
    /// @ref Component is accepted, e.g. `chunk.mask.has(id.componentBit())`.
    Component componentBit() const noexcept {
        return static_cast<Component>(1ull << bit);
    }
};

/// Record an "attach this user component" mutation. The presence bit is
/// always set on commit (regardless of value contents), mirroring the
/// built-in @ref CommandBuffer::addComponent semantics.
///
/// @pre @p id was obtained from `Engine::registerUserComponent<T>()`.
/// @pre `T` is trivially copyable (the engine memcpys the value).
/// @pre `sizeof(T) == id.stride` (asserted in debug; UB otherwise).
template <typename T>
inline void addUserComponent(CommandBuffer& cb, UserComponentId id,
                             EntityHandle entity, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>,
        "addUserComponent<T>: T must be trivially copyable.");
    assert(id.valid() && "addUserComponent: UserComponentId not valid — register first");
    assert(id.stride == sizeof(T) && "addUserComponent: stride mismatch — wrong T for this id");
    // §3.9.3 batch 18 — heap-allocate `CmdAddUserComponent` so the
    // 112-byte payload doesn't bloat every other variant alternative.
    auto c = std::make_unique<detail::CmdAddUserComponent>();
    c->entity = entity;
    c->bit    = id.bit;
    c->stride = id.stride;
    if (sizeof(T) <= c->inline_.size()) {
        std::memcpy(c->inline_.data(), &value, sizeof(T));
        c->size = static_cast<std::uint32_t>(sizeof(T));
    } else {
        c->heap.resize(sizeof(T));
        std::memcpy(c->heap.data(), &value, sizeof(T));
        c->size = 0;
    }
    cb.commands().emplace_back(std::move(c));
    cb.noteGlobalCommand(static_cast<std::uint32_t>(cb.commands().size() - 1));
}

/// Record a "detach this user component" mutation. The presence bit is
/// cleared on commit; the entity migrates out of the user-component-
/// carrying chunk. A subsequent @ref user::tryGet returns nullptr.
///
/// Idempotent — removing an absent user component is a no-op.
inline void removeUserComponent(CommandBuffer& cb, UserComponentId id,
                                EntityHandle entity) {
    assert(id.valid() && "removeUserComponent: UserComponentId not valid — register first");
    detail::CmdRemoveUserComponent c;
    c.entity = entity;
    c.bit    = id.bit;
    cb.commands().emplace_back(c);
    cb.noteGlobalCommand(static_cast<std::uint32_t>(cb.commands().size() - 1));
}

namespace user {

/// True iff @p e is alive AND its archetype carries the user component
/// identified by @p id. O(1).
inline bool has(const World& w, UserComponentId id, EntityHandle e) noexcept {
    const auto* m = w.tryGetComponentMask(e);
    return m != nullptr && m->has(id.componentBit());
}

/// Pointer to @p e's current user-component value, or nullptr if absent.
/// Returned pointer is valid until the next mutation of the world. O(1).
template <typename T>
inline const T* tryGet(const World& w, UserComponentId id,
                       EntityHandle e) noexcept {
    static_assert(std::is_trivially_copyable_v<T>,
        "user::tryGet<T>: T must be trivially copyable.");
    assert(id.stride == sizeof(T) && "user::tryGet: stride mismatch — wrong T");
    if (!has(w, id, e)) return nullptr;
    const auto loc = w.locate(e);
    if (loc.archetype >= w.archetypeChunkCount()) return nullptr;
    const auto& chunk = w.archetypeChunk(loc.archetype);
    const auto* col = chunk.findUserColumn(id.bit);
    if (!col) return nullptr;
    return reinterpret_cast<const T*>(col->bytes.data()
        + static_cast<std::size_t>(loc.row) * col->stride);
}

/// Typed read-only span over the user component's storage in one chunk.
/// Returns an empty span if the chunk's mask does not carry @p id. The
/// span aliases the chunk's storage and is invalidated by any mutation
/// — read it inside the same `forEachChunk` callback.
template <typename T>
inline std::span<const T> chunkSpan(const internal::ArchetypeChunk& chunk,
                                    UserComponentId id) noexcept {
    static_assert(std::is_trivially_copyable_v<T>,
        "user::chunkSpan<T>: T must be trivially copyable.");
    assert(id.stride == sizeof(T) && "user::chunkSpan: stride mismatch — wrong T");
    const auto* col = chunk.findUserColumn(id.bit);
    if (!col) return {};
    return std::span<const T>(
        reinterpret_cast<const T*>(col->bytes.data()),
        col->bytes.size() / sizeof(T));
}

} // namespace user
} // namespace threadmaxx
