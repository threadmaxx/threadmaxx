#pragma once

#include "Handles.hpp"
#include "Resource.hpp"

#include <cstdint>
#include <initializer_list>

namespace threadmaxx {

/// Categories of state a system can declare it reads or writes, AND the
/// per-entity component-presence bits stored alongside dense data.
///
/// The engine uses these for two distinct things:
///   1. **Scheduling**: a system declares which components it `reads()`
///      and `writes()`; non-conflicting systems share a wave.
///   2. **Presence**: every entity carries a @ref ComponentSet of which
///      built-in components are logically attached, so renderers and
///      queries can skip absent ones.
///
/// `EntityStructural` is only meaningful in the scheduling role; it
/// does not appear in per-entity masks.
///
/// The underlying type is @c std::uint64_t — 64 distinct categories,
/// of which the first 16 are currently allocated.
enum class Component : std::uint64_t {
    Transform         = 1ull << 0,
    Velocity          = 1ull << 1,
    RenderTag         = 1ull << 2,
    UserData          = 1ull << 3,
    EntityStructural  = 1ull << 4,  ///< Scheduling only: "this system touches the set of live entities".
    Acceleration      = 1ull << 5,
    Parent            = 1ull << 6,
    Health            = 1ull << 7,
    Faction           = 1ull << 8,
    AnimationStateRef = 1ull << 9,
    PhysicsBodyRef    = 1ull << 10,
    NavAgentRef       = 1ull << 11,
    BoundingVolume    = 1ull << 12,
    StaticTag         = 1ull << 13,
    DisabledTag       = 1ull << 14,
    DestroyedTag      = 1ull << 15,
};

/// Bitset over @ref Component values. Trivially copyable, no allocation.
class ComponentSet {
public:
    constexpr ComponentSet() noexcept = default;
    constexpr ComponentSet(Component c) noexcept
        : bits_(static_cast<std::uint64_t>(c)) {}
    constexpr ComponentSet(std::initializer_list<Component> cs) noexcept {
        for (auto c : cs) bits_ |= static_cast<std::uint64_t>(c);
    }

    /// Universal set (all currently-allocated bits). Default for
    /// `ISystem::reads()` / `writes()` — makes every pair of systems
    /// conflict. Keep in sync with the @ref Component enum: bits 0..15
    /// are allocated as of batch 5.
    static constexpr ComponentSet all() noexcept {
        ComponentSet s;
        s.bits_ = 0xFFFFull;  // bits 0..15 — keep in sync with Component
        return s;
    }
    static constexpr ComponentSet none() noexcept { return ComponentSet{}; }

    constexpr std::uint64_t bits()  const noexcept { return bits_; }
    constexpr bool          empty() const noexcept { return bits_ == 0; }

    /// True iff the two sets share at least one bit.
    constexpr bool intersects(ComponentSet o) const noexcept {
        return (bits_ & o.bits_) != 0;
    }

    /// True iff bit `c` is set.
    constexpr bool has(Component c) const noexcept {
        return (bits_ & static_cast<std::uint64_t>(c)) != 0;
    }

    /// True iff every bit in `o` is set in `*this`.
    constexpr bool hasAll(ComponentSet o) const noexcept {
        return (bits_ & o.bits_) == o.bits_;
    }

    constexpr ComponentSet operator|(ComponentSet o) const noexcept {
        ComponentSet r; r.bits_ = bits_ | o.bits_; return r;
    }
    constexpr ComponentSet& operator|=(ComponentSet o) noexcept {
        bits_ |= o.bits_; return *this;
    }
    constexpr ComponentSet operator&(ComponentSet o) const noexcept {
        ComponentSet r; r.bits_ = bits_ & o.bits_; return r;
    }
    constexpr ComponentSet& add(Component c) noexcept {
        bits_ |= static_cast<std::uint64_t>(c); return *this;
    }
    constexpr ComponentSet& remove(Component c) noexcept {
        bits_ &= ~static_cast<std::uint64_t>(c); return *this;
    }
    constexpr bool operator==(const ComponentSet&) const noexcept = default;

private:
    std::uint64_t bits_ = 0;
};

constexpr ComponentSet operator|(Component a, Component b) noexcept {
    return ComponentSet{a} | ComponentSet{b};
}

/// 3-component vector. Trivially copyable POD.
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

/// Quaternion `xi + yj + zk + w`. Identity is `{0,0,0,1}`.
struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

/// World-space pose. The engine never integrates @ref Velocity into this;
/// user systems do.
struct Transform {
    Vec3 position;
    Quat orientation;
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

/// Per-tick rate. User systems integrate this into @ref Transform.
struct Velocity {
    Vec3 linear;
    Vec3 angular;
};

/// Per-tick rate-of-change applied to @ref Velocity by user systems.
/// @note The engine itself does not integrate this — it's a data field
///       that physics-style systems read to update Velocity, which in
///       turn drives Transform.
struct Acceleration {
    Vec3 linear;
    Vec3 angular;
};

/// Lightweight tag the renderer keys off of.
/// @note A negative `meshId` means the entity is not renderable; the
///       default presence-mask derivation in `CommandBuffer::spawn`
///       sets the RenderTag bit iff `meshId >= 0`.
struct RenderTag {
    std::int32_t meshId = -1;
    std::int32_t materialId = -1;
    std::uint32_t flags = 0;
};

/// User-controlled 64 bits per entity. Useful for AI state, faction IDs,
/// etc. The engine never interprets this value.
struct UserData {
    std::uint64_t value = 0;
};

/// Hierarchical attachment.
///
/// The built-in @ref makeHierarchySystem reads this plus the parent's
/// world @ref Transform, then writes the child's world @ref Transform
/// as `parent_world ∘ localOffset`.
///
/// @warning Scale is NOT chained. A child's world scale equals
///          `localOffset.scale`, ignoring the parent's scale. See
///          `doc/hierarchy.md` for rationale.
/// @note `parent == kInvalidEntity` makes the hierarchy system treat
///       the entity as a root even if the Parent presence bit is set.
struct Parent {
    EntityHandle parent      = kInvalidEntity;
    Transform    localOffset = {};
};

/// Per-entity hit-point pool. The engine never integrates this; user
/// systems (damage, regen, death) own the math. Capped at @c max by
/// convention but the engine does not enforce it.
struct Health {
    float current = 0.0f;
    float max     = 0.0f;
};

/// Per-entity faction / team id. The engine never interprets this; AI
/// systems use it for friend/foe checks.
struct Faction {
    std::uint32_t id = 0;
};

/// Phantom tag identifying an animation graph resource. Used as the
/// type parameter of `ResourceId<AnimationGraph>` in
/// @ref AnimationStateRef. The engine never instantiates the type — it
/// is only used as a compile-time brand on the resource id.
struct AnimationGraph;

/// Per-entity animation state reference. Points at a resource-managed
/// animation graph plus the entity's current state within it and the
/// elapsed time since that state began. The engine never integrates
/// this — a user-side animation system reads it and writes the
/// resulting pose into a future @ref AnimationPose component (deferred
/// to §3.4 batch 8).
struct AnimationStateRef {
    ResourceId<AnimationGraph> graph;
    std::uint32_t              state = 0;
    float                      t     = 0.0f;
};

/// Opaque per-entity reference to a physics body owned by a sibling
/// physics library (Bullet, Jolt, PhysX, …). The engine never
/// interprets the handle; user systems read positions out of and write
/// positions into the foreign world via this opaque value.
struct PhysicsBodyRef {
    std::uint64_t handle = 0;
};

/// Opaque per-entity reference to a navigation agent owned by a
/// sibling navmesh library. Same shape and reasoning as
/// @ref PhysicsBodyRef.
struct NavAgentRef {
    std::uint64_t handle = 0;
};

/// Axis-aligned bounding volume in world space. Visibility-culling and
/// broad-phase systems consume this; the engine itself does not
/// populate or maintain it (a render-prep system or the physics body
/// usually does).
struct BoundingVolume {
    Vec3 min;
    Vec3 max;
};

} // namespace threadmaxx
