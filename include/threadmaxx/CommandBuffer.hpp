#pragma once

#include "Components.hpp"
#include "Handles.hpp"

#include <type_traits>
#include <variant>
#include <vector>

namespace threadmaxx {

/// Recording-side sugar that packages a set of component values together
/// with a compile-time-derived component-presence mask. Build one with
/// the variadic @ref bundle factory and feed it to
/// @ref CommandBuffer::spawn — zero runtime cost compared with calling
/// the 7-arg `spawn()` overload by hand.
///
/// Unlike the default-mask overloads of `CommandBuffer::spawn`, a
/// `Bundle` only sets the presence bits for components the caller
/// explicitly listed: `bundle(Transform{}, Velocity{})` produces an
/// entity with just Transform+Velocity bits, not Transform+Velocity+
/// UserData+Acceleration.
struct Bundle {
    Transform         transform        = {};
    Velocity          velocity         = {};
    RenderTag         renderTag        = {};
    UserData          userData         = {};
    Acceleration      acceleration     = {};
    Parent            parent           = {};
    Health            health           = {};
    Faction           faction          = {};
    AnimationStateRef animationState   = {};
    PhysicsBodyRef    physicsBody      = {};
    NavAgentRef       navAgent         = {};
    BoundingVolume    boundingVolume   = {};
    /// Set of component-presence bits derived from the parameter pack
    /// passed to @ref bundle. The engine writes this verbatim into the
    /// new entity's per-entity mask.
    ComponentSet initialMask  = {};
};

namespace detail {

template <typename T>
constexpr Component bundleComponentBit() noexcept {
    if constexpr (std::is_same_v<T, Transform>)              return Component::Transform;
    else if constexpr (std::is_same_v<T, Velocity>)          return Component::Velocity;
    else if constexpr (std::is_same_v<T, RenderTag>)         return Component::RenderTag;
    else if constexpr (std::is_same_v<T, UserData>)          return Component::UserData;
    else if constexpr (std::is_same_v<T, Acceleration>)      return Component::Acceleration;
    else if constexpr (std::is_same_v<T, Parent>)            return Component::Parent;
    else if constexpr (std::is_same_v<T, Health>)            return Component::Health;
    else if constexpr (std::is_same_v<T, Faction>)           return Component::Faction;
    else if constexpr (std::is_same_v<T, AnimationStateRef>) return Component::AnimationStateRef;
    else if constexpr (std::is_same_v<T, PhysicsBodyRef>)    return Component::PhysicsBodyRef;
    else if constexpr (std::is_same_v<T, NavAgentRef>)       return Component::NavAgentRef;
    else if constexpr (std::is_same_v<T, BoundingVolume>)    return Component::BoundingVolume;
    else static_assert(sizeof(T) == 0,
        "bundle(): argument type must be one of Transform, Velocity, "
        "RenderTag, UserData, Acceleration, Parent, Health, Faction, "
        "AnimationStateRef, PhysicsBodyRef, NavAgentRef, BoundingVolume");
}

template <typename T>
constexpr void bundleStore(Bundle& b, const T& v) noexcept {
    if constexpr (std::is_same_v<T, Transform>)              b.transform      = v;
    else if constexpr (std::is_same_v<T, Velocity>)          b.velocity       = v;
    else if constexpr (std::is_same_v<T, RenderTag>)         b.renderTag      = v;
    else if constexpr (std::is_same_v<T, UserData>)          b.userData       = v;
    else if constexpr (std::is_same_v<T, Acceleration>)      b.acceleration   = v;
    else if constexpr (std::is_same_v<T, Parent>)            b.parent         = v;
    else if constexpr (std::is_same_v<T, Health>)            b.health         = v;
    else if constexpr (std::is_same_v<T, Faction>)           b.faction        = v;
    else if constexpr (std::is_same_v<T, AnimationStateRef>) b.animationState = v;
    else if constexpr (std::is_same_v<T, PhysicsBodyRef>)    b.physicsBody    = v;
    else if constexpr (std::is_same_v<T, NavAgentRef>)       b.navAgent       = v;
    else if constexpr (std::is_same_v<T, BoundingVolume>)    b.boundingVolume = v;
}

} // namespace detail

/// Build a @ref Bundle from a parameter pack of distinct component
/// values. The resulting `initialMask` is the union of the bits for
/// each `Cs` — duplicate types are allowed but pointless (later values
/// overwrite earlier ones).
///
/// @code
/// auto enemy = bundle(Transform{}, Velocity{}, RenderTag{1});
/// cb.spawnBundle(enemy);                 // fresh slot
/// cb.spawnBundle(reservedHandle, enemy); // pre-reserved slot
/// @endcode
template <typename... Cs>
constexpr Bundle bundle(const Cs&... values) noexcept {
    Bundle b;
    ((b.initialMask |= ComponentSet{detail::bundleComponentBit<Cs>()}), ...);
    (detail::bundleStore(b, values), ...);
    return b;
}

namespace detail {

/// @internal Spawn command payload. The engine derives a default
/// component-presence mask in `CommandBuffer::spawn()` if the caller
/// does not provide one explicitly.
struct CmdSpawn {
    Transform         transform;
    Velocity          velocity;
    RenderTag         render;
    UserData          userData;
    Acceleration      acceleration;
    Parent            parent;
    Health            health;
    Faction           faction;
    AnimationStateRef animationState;
    PhysicsBodyRef    physicsBody;
    NavAgentRef       navAgent;
    BoundingVolume    boundingVolume;
    /// Bitset of which components are logically present on this entity.
    /// Defaults are filled in by `CommandBuffer::spawn()` from the
    /// supplied values (RenderTag bit iff `render.meshId >= 0`; Parent
    /// bit iff `parent.parent.valid()`; Transform/Velocity/UserData/
    /// Acceleration always; the §3.1 batch-5 slots are off by default
    /// and only attach when the caller uses an explicit mask or a
    /// @ref Bundle).
    ComponentSet initialMask;
    /// If valid, materialize a slot previously obtained via
    /// `SystemContext::reserveHandle()` instead of allocating a fresh
    /// one. Falls back to a fresh allocation if the reservation has
    /// already been consumed or discarded.
    EntityHandle reserved = kInvalidEntity;
};
struct CmdDestroy            { EntityHandle entity; };
struct CmdSetTransform       { EntityHandle entity; Transform         value; };
struct CmdSetVelocity        { EntityHandle entity; Velocity          value; };
struct CmdSetRenderTag       { EntityHandle entity; RenderTag         value; };
struct CmdSetUserData        { EntityHandle entity; UserData          value; };
struct CmdSetAcceleration    { EntityHandle entity; Acceleration      value; };
struct CmdSetParent          { EntityHandle entity; Parent            value; };
struct CmdSetHealth          { EntityHandle entity; Health            value; };
struct CmdSetFaction         { EntityHandle entity; Faction           value; };
struct CmdSetAnimationState  { EntityHandle entity; AnimationStateRef value; };
struct CmdSetPhysicsBody     { EntityHandle entity; PhysicsBodyRef    value; };
struct CmdSetNavAgent        { EntityHandle entity; NavAgentRef       value; };
struct CmdSetBoundingVolume  { EntityHandle entity; BoundingVolume    value; };
struct CmdSetComponentMask   { EntityHandle entity; ComponentSet      value; };
struct CmdAddTag             { EntityHandle entity; Component         tag;   };
struct CmdRemoveTag          { EntityHandle entity; Component         tag;   };

using Command = std::variant<
    CmdSpawn,
    CmdDestroy,
    CmdSetTransform,
    CmdSetVelocity,
    CmdSetRenderTag,
    CmdSetUserData,
    CmdSetAcceleration,
    CmdSetParent,
    CmdSetHealth,
    CmdSetFaction,
    CmdSetAnimationState,
    CmdSetPhysicsBody,
    CmdSetNavAgent,
    CmdSetBoundingVolume,
    CmdSetComponentMask,
    CmdAddTag,
    CmdRemoveTag>;

} // namespace detail

/// Per-job recorder of world mutations.
///
/// A worker thread owns one CommandBuffer for the duration of its job
/// and records mutations into it. The engine collects all command
/// buffers after the batch completes and applies them in deterministic
/// submission order on the simulation thread.
///
/// There are no locks here on purpose: each instance is exclusive to
/// one worker for the lifetime of a job.
class CommandBuffer {
public:
    CommandBuffer() = default;
    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;
    CommandBuffer(CommandBuffer&&) noexcept = default;
    CommandBuffer& operator=(CommandBuffer&&) noexcept = default;

    /// Spawn with an automatically-derived component mask: Transform,
    /// Velocity, UserData and Acceleration are always set; RenderTag is
    /// set iff `r.meshId >= 0`; Parent is set iff `p.parent.valid()`.
    /// The §3.1 batch-5 slots (Health, Faction, AnimationStateRef,
    /// PhysicsBodyRef, NavAgentRef, BoundingVolume) are *not* attached
    /// by default — use the explicit-mask overload or @ref spawnBundle
    /// when you need them.
    void spawn(const Transform& t, const Velocity& v = {},
               const RenderTag& r = {}, const UserData& u = {},
               const Acceleration& a = {}, const Parent& p = {});

    /// Spawn with an explicit initial component mask and an optional
    /// parent.
    void spawn(const Transform& t, const Velocity& v, const RenderTag& r,
               const UserData& u, const Acceleration& a,
               const Parent& p, ComponentSet initialMask);

    /// Spawn into a pre-reserved slot taken via
    /// `SystemContext::reserveHandle()`. Use this when a single job needs
    /// to know the handle in advance (e.g. to spawn a parent and a child
    /// in the same recording). The reservation is consumed on commit;
    /// any reservation not consumed by step end is reaped.
    /// @pre `reserved` was returned by `SystemContext::reserveHandle()`
    ///      in this same step's recording phase.
    void spawn(EntityHandle reserved, const Transform& t,
               const Velocity& v = {}, const RenderTag& r = {},
               const UserData& u = {}, const Acceleration& a = {},
               const Parent& p = {});

    /// As above with an explicit initial component mask and an optional
    /// parent.
    void spawn(EntityHandle reserved, const Transform& t, const Velocity& v,
               const RenderTag& r, const UserData& u, const Acceleration& a,
               const Parent& p, ComponentSet initialMask);

    /// Spawn a @ref Bundle (§3.5). The bundle carries its own
    /// component-presence mask derived at the @ref bundle call site.
    /// Named distinctly from `spawn` so a braced-init call like
    /// `cb.spawn({})` stays unambiguous against the Transform-first
    /// overload.
    void spawnBundle(const Bundle& b);

    /// Spawn a @ref Bundle into a pre-reserved handle taken via
    /// `SystemContext::reserveHandle()`.
    void spawnBundle(EntityHandle reserved, const Bundle& b);

    void destroy(EntityHandle entity);
    void setTransform   (EntityHandle entity, const Transform&    t);
    void setVelocity    (EntityHandle entity, const Velocity&     v);

    /// Writes the RenderTag value AND updates the entity's RenderTag
    /// presence bit (set if `r.meshId >= 0`, cleared otherwise).
    void setRenderTag   (EntityHandle entity, const RenderTag&    r);

    void setUserData    (EntityHandle entity, const UserData&     u);
    void setAcceleration(EntityHandle entity, const Acceleration& a);

    /// Writes the Parent value AND updates the entity's Parent presence
    /// bit (set if `p.parent.valid()`, cleared otherwise).
    void setParent      (EntityHandle entity, const Parent&       p);

    /// @name §3.1 batch-5 setters
    /// Each writes the dense-array value AND sets the corresponding
    /// component-presence bit. There is no "auto-derive a bit from the
    /// value" convention for these — opting an entity into Health,
    /// Faction, etc. is always an explicit decision by gameplay code.
    /// @{
    void setHealth          (EntityHandle entity, const Health&            h);
    void setFaction         (EntityHandle entity, const Faction&           f);
    void setAnimationStateRef(EntityHandle entity, const AnimationStateRef& a);
    void setPhysicsBodyRef  (EntityHandle entity, const PhysicsBodyRef&    p);
    void setNavAgentRef     (EntityHandle entity, const NavAgentRef&       n);
    void setBoundingVolume  (EntityHandle entity, const BoundingVolume&    b);
    /// @}

    /// Directly overwrite the entity's component mask. Use for cases
    /// the automatic spawn/setRenderTag/setParent derivation does not
    /// cover; the engine does not validate consistency. For single-bit
    /// edits prefer @ref addTag / @ref removeTag — they compose
    /// correctly when multiple workers each flip a different bit on
    /// the same entity within a tick, whereas this method clobbers
    /// the whole mask in submission order.
    void setComponentMask(EntityHandle entity, ComponentSet mask);

    /// Attach a tag-only category (`Component::StaticTag`,
    /// `DisabledTag`, `DestroyedTag`) — or any component bit you want
    /// to flip without rewriting the rest of the mask. The commit
    /// phase ORs the bit in. Independent tag flips from different
    /// workers in the same tick all land.
    void addTag(EntityHandle entity, Component tag);

    /// Inverse of @ref addTag — clears the bit.
    void removeTag(EntityHandle entity, Component tag);

    void reserve(std::size_t n)        { commands_.reserve(n); }
    void clear() noexcept              { commands_.clear(); }
    std::size_t size() const noexcept  { return commands_.size(); }
    bool empty() const noexcept        { return commands_.empty(); }

    /// @internal Used internally by the commit phase.
    const std::vector<detail::Command>& commands() const noexcept { return commands_; }
    /// @internal Used internally by the commit phase.
    std::vector<detail::Command>& commands() noexcept { return commands_; }

private:
    std::vector<detail::Command> commands_;
};

} // namespace threadmaxx
