#pragma once

#include "Handles.hpp"

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
enum class Component : std::uint32_t {
    Transform        = 1u << 0,
    Velocity         = 1u << 1,
    RenderTag        = 1u << 2,
    UserData         = 1u << 3,
    EntityStructural = 1u << 4,   ///< Scheduling only: "this system touches the set of live entities".
    Acceleration     = 1u << 5,
    Parent           = 1u << 6,
};

/// Bitset over @ref Component values. Trivially copyable, no allocation.
class ComponentSet {
public:
    constexpr ComponentSet() noexcept = default;
    constexpr ComponentSet(Component c) noexcept
        : bits_(static_cast<std::uint32_t>(c)) {}
    constexpr ComponentSet(std::initializer_list<Component> cs) noexcept {
        for (auto c : cs) bits_ |= static_cast<std::uint32_t>(c);
    }

    /// Universal set (bits 0..6). Default for `ISystem::reads()` /
    /// `writes()` — makes every pair of systems conflict.
    static constexpr ComponentSet all() noexcept {
        ComponentSet s;
        s.bits_ = 0x7Fu;  // bits 0..6 — keep in sync with Component
        return s;
    }
    static constexpr ComponentSet none() noexcept { return ComponentSet{}; }

    constexpr std::uint32_t bits()  const noexcept { return bits_; }
    constexpr bool          empty() const noexcept { return bits_ == 0; }

    /// True iff the two sets share at least one bit.
    constexpr bool intersects(ComponentSet o) const noexcept {
        return (bits_ & o.bits_) != 0;
    }

    /// True iff bit `c` is set.
    constexpr bool has(Component c) const noexcept {
        return (bits_ & static_cast<std::uint32_t>(c)) != 0;
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
        bits_ |= static_cast<std::uint32_t>(c); return *this;
    }
    constexpr ComponentSet& remove(Component c) noexcept {
        bits_ &= ~static_cast<std::uint32_t>(c); return *this;
    }
    constexpr bool operator==(const ComponentSet&) const noexcept = default;

private:
    std::uint32_t bits_ = 0;
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

} // namespace threadmaxx
