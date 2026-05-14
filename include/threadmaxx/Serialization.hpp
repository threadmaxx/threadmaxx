#pragma once

#include "Components.hpp"
#include "Handles.hpp"

#include <cstdint>
#include <cstring>
#include <istream>
#include <ostream>
#include <type_traits>
#include <vector>

namespace threadmaxx {

/// @file Serialization.hpp
/// Header-only binary serialization for the built-in component types
/// and a @ref WorldSnapshot bundle that captures the world's dense
/// arrays in one shot.
///
/// The intent is to give game code a stable, low-friction place to
/// hook save/load and net replication without the engine committing to
/// a wire format. The default implementation here writes raw bytes for
/// each POD field plus a tiny header (magic + version + per-array
/// length). Games that want JSON, protobuf, or migration support can
/// build it on top — every component type has a free-function
/// overload pair, so adding a new format is a header-only specialization.
///
/// @par Endianness
///      All multi-byte values are written host-endian. Snapshots are
///      not portable across architectures of different endianness;
///      treat them as an opaque savegame format on a single platform.
///      A cross-platform format is a §3.3 batch 7 concern.

namespace detail {

template <typename T>
inline void writePod(std::ostream& os, const T& v) {
    static_assert(std::is_trivially_copyable_v<T>,
                  "writePod requires a trivially-copyable type");
    os.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T>
inline bool readPod(std::istream& is, T& v) {
    static_assert(std::is_trivially_copyable_v<T>,
                  "readPod requires a trivially-copyable type");
    is.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<bool>(is);
}

template <typename T>
inline void writeVec(std::ostream& os, const std::vector<T>& v) {
    const std::uint64_t n = v.size();
    writePod(os, n);
    if (n != 0) {
        os.write(reinterpret_cast<const char*>(v.data()),
                 static_cast<std::streamsize>(sizeof(T) * n));
    }
}

template <typename T>
inline bool readVec(std::istream& is, std::vector<T>& v) {
    std::uint64_t n = 0;
    if (!readPod(is, n)) return false;
    v.resize(static_cast<std::size_t>(n));
    if (n != 0) {
        is.read(reinterpret_cast<char*>(v.data()),
                static_cast<std::streamsize>(sizeof(T) * n));
    }
    return static_cast<bool>(is);
}

} // namespace detail

/// Per-component serialization trait pair. The defaults below write the
/// raw bytes of the POD; specialize (or shadow with a free function in
/// the game's namespace) when a non-trivial format is required.
inline void serialize  (std::ostream& os, const Vec3& v) { detail::writePod(os, v); }
inline bool deserialize(std::istream& is,       Vec3& v) { return detail::readPod(is, v); }

inline void serialize  (std::ostream& os, const Quat& q) { detail::writePod(os, q); }
inline bool deserialize(std::istream& is,       Quat& q) { return detail::readPod(is, q); }

inline void serialize  (std::ostream& os, const Transform& t) { detail::writePod(os, t); }
inline bool deserialize(std::istream& is,       Transform& t) { return detail::readPod(is, t); }

inline void serialize  (std::ostream& os, const Velocity& v) { detail::writePod(os, v); }
inline bool deserialize(std::istream& is,       Velocity& v) { return detail::readPod(is, v); }

inline void serialize  (std::ostream& os, const Acceleration& a) { detail::writePod(os, a); }
inline bool deserialize(std::istream& is,       Acceleration& a) { return detail::readPod(is, a); }

inline void serialize  (std::ostream& os, const RenderTag& r) { detail::writePod(os, r); }
inline bool deserialize(std::istream& is,       RenderTag& r) { return detail::readPod(is, r); }

inline void serialize  (std::ostream& os, const UserData& u) { detail::writePod(os, u); }
inline bool deserialize(std::istream& is,       UserData& u) { return detail::readPod(is, u); }

inline void serialize  (std::ostream& os, const Parent& p) { detail::writePod(os, p); }
inline bool deserialize(std::istream& is,       Parent& p) { return detail::readPod(is, p); }

inline void serialize  (std::ostream& os, const Health& h) { detail::writePod(os, h); }
inline bool deserialize(std::istream& is,       Health& h) { return detail::readPod(is, h); }

inline void serialize  (std::ostream& os, const Faction& f) { detail::writePod(os, f); }
inline bool deserialize(std::istream& is,       Faction& f) { return detail::readPod(is, f); }

inline void serialize  (std::ostream& os, const AnimationStateRef& a) { detail::writePod(os, a); }
inline bool deserialize(std::istream& is,       AnimationStateRef& a) { return detail::readPod(is, a); }

inline void serialize  (std::ostream& os, const PhysicsBodyRef& p) { detail::writePod(os, p); }
inline bool deserialize(std::istream& is,       PhysicsBodyRef& p) { return detail::readPod(is, p); }

inline void serialize  (std::ostream& os, const NavAgentRef& n) { detail::writePod(os, n); }
inline bool deserialize(std::istream& is,       NavAgentRef& n) { return detail::readPod(is, n); }

inline void serialize  (std::ostream& os, const BoundingVolume& b) { detail::writePod(os, b); }
inline bool deserialize(std::istream& is,       BoundingVolume& b) { return detail::readPod(is, b); }

inline void serialize(std::ostream& os, const ComponentSet& m) {
    const std::uint64_t bits = m.bits();
    detail::writePod(os, bits);
}
inline bool deserialize(std::istream& is, ComponentSet& m) {
    std::uint64_t bits = 0;
    if (!detail::readPod(is, bits)) return false;
    // Rebuild via the public API: clear, then set every present bit.
    m = ComponentSet{};
    for (auto c : { Component::Transform, Component::Velocity,
                    Component::RenderTag, Component::UserData,
                    Component::Acceleration, Component::Parent,
                    Component::Health, Component::Faction,
                    Component::AnimationStateRef, Component::PhysicsBodyRef,
                    Component::NavAgentRef, Component::BoundingVolume,
                    Component::StaticTag, Component::DisabledTag,
                    Component::DestroyedTag }) {
        if (bits & static_cast<std::uint64_t>(c)) m.add(c);
    }
    return true;
}

/// Frozen copy of the world's dense arrays at the moment
/// @ref World::snapshot was called. Every vector has the same length
/// (`size()`); index `i` of each array describes the same entity.
///
/// This is *data*, not a recipe for replay. Restoring requires the
/// game to walk the snapshot and call `cb.spawn(...)` per entry; the
/// engine intentionally does not provide a `load` that bypasses the
/// command-buffer commit phase. See `doc/serialization.md` for the
/// pattern.
struct WorldSnapshot {
    std::vector<EntityHandle>      entities;
    std::vector<Transform>         transforms;
    std::vector<Velocity>          velocities;
    std::vector<RenderTag>         renderTags;
    std::vector<UserData>          userData;
    std::vector<Acceleration>      accelerations;
    std::vector<Parent>            parents;
    std::vector<Health>            healths;
    std::vector<Faction>           factions;
    std::vector<AnimationStateRef> animationStates;
    std::vector<PhysicsBodyRef>    physicsBodies;
    std::vector<NavAgentRef>       navAgents;
    std::vector<BoundingVolume>    boundingVolumes;
    std::vector<ComponentSet>      masks;

    std::size_t size() const noexcept { return entities.size(); }
};

/// Wire-format magic and version. Bump @ref kWorldSnapshotVersion on
/// breaking changes — the deserializer rejects mismatched versions.
///
/// History:
///   - v1: 8 dense arrays, 32-bit ComponentSet mask. Pre-batch-5.
///   - v2: 14 dense arrays (added Health, Faction, AnimationStateRef,
///         PhysicsBodyRef, NavAgentRef, BoundingVolume), 64-bit
///         ComponentSet mask. Tag-only components ride in the mask
///         bits, no dense storage. Batch 5.
inline constexpr std::uint32_t kWorldSnapshotMagic   = 0x544D5853u; // 'SXMT' (LE)
inline constexpr std::uint32_t kWorldSnapshotVersion = 2u;

/// Write a @ref WorldSnapshot as a self-describing binary blob:
/// `[magic u32][version u32][entityCount u64][14 dense arrays...]`.
inline void serialize(std::ostream& os, const WorldSnapshot& s) {
    detail::writePod(os, kWorldSnapshotMagic);
    detail::writePod(os, kWorldSnapshotVersion);
    const std::uint64_t n = s.entities.size();
    detail::writePod(os, n);
    detail::writeVec(os, s.entities);
    detail::writeVec(os, s.transforms);
    detail::writeVec(os, s.velocities);
    detail::writeVec(os, s.renderTags);
    detail::writeVec(os, s.userData);
    detail::writeVec(os, s.accelerations);
    detail::writeVec(os, s.parents);
    detail::writeVec(os, s.healths);
    detail::writeVec(os, s.factions);
    detail::writeVec(os, s.animationStates);
    detail::writeVec(os, s.physicsBodies);
    detail::writeVec(os, s.navAgents);
    detail::writeVec(os, s.boundingVolumes);
    detail::writeVec(os, s.masks);
}

/// Read a @ref WorldSnapshot written by @ref serialize. Returns false
/// if the magic or version mismatch, or if the stream ran out before a
/// complete record was read. On failure the snapshot is left in a
/// partially-filled state — discard it.
inline bool deserialize(std::istream& is, WorldSnapshot& s) {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint64_t count = 0;
    if (!detail::readPod(is, magic))   return false;
    if (magic != kWorldSnapshotMagic)  return false;
    if (!detail::readPod(is, version)) return false;
    if (version != kWorldSnapshotVersion) return false;
    if (!detail::readPod(is, count))   return false;

    if (!detail::readVec(is, s.entities))        return false;
    if (!detail::readVec(is, s.transforms))      return false;
    if (!detail::readVec(is, s.velocities))      return false;
    if (!detail::readVec(is, s.renderTags))      return false;
    if (!detail::readVec(is, s.userData))        return false;
    if (!detail::readVec(is, s.accelerations))   return false;
    if (!detail::readVec(is, s.parents))         return false;
    if (!detail::readVec(is, s.healths))         return false;
    if (!detail::readVec(is, s.factions))        return false;
    if (!detail::readVec(is, s.animationStates)) return false;
    if (!detail::readVec(is, s.physicsBodies))   return false;
    if (!detail::readVec(is, s.navAgents))       return false;
    if (!detail::readVec(is, s.boundingVolumes)) return false;
    if (!detail::readVec(is, s.masks))           return false;

    // Sanity: the header count should match every array's length. If
    // it doesn't, the file is corrupt.
    if (s.entities.size() != count)        return false;
    if (s.transforms.size() != count)      return false;
    if (s.velocities.size() != count)      return false;
    if (s.renderTags.size() != count)      return false;
    if (s.userData.size() != count)        return false;
    if (s.accelerations.size() != count)   return false;
    if (s.parents.size() != count)         return false;
    if (s.healths.size() != count)         return false;
    if (s.factions.size() != count)        return false;
    if (s.animationStates.size() != count) return false;
    if (s.physicsBodies.size() != count)   return false;
    if (s.navAgents.size() != count)       return false;
    if (s.boundingVolumes.size() != count) return false;
    if (s.masks.size() != count)           return false;
    return true;
}

} // namespace threadmaxx
