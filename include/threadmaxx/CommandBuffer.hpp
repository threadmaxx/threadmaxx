#pragma once

#include "Components.hpp"
#include "Handles.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

namespace threadmaxx {

namespace detail {

/// @internal Forward-declared in CommandBuffer.hpp so the variant can
/// include it; the body lives in @ref UserComponent.hpp.
struct CmdAddUserComponent {
    EntityHandle               entity;
    std::uint32_t              bit    = 0;
    std::uint32_t              stride = 0;
    std::array<std::byte, 64>  inline_;
    std::uint32_t              size   = 0;
    std::vector<std::byte>     heap;

    const std::byte* data() const noexcept {
        return size > 0 ? inline_.data() : heap.data();
    }
};
struct CmdRemoveUserComponent {
    EntityHandle  entity;
    std::uint32_t bit = 0;
};

} // namespace detail

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

    /// §3.10.2 batch 22 — F10 fix. Builder-style helper that sets the
    /// per-field value AND attaches the matching presence bit in
    /// `initialMask`, in one call. Composes for chaining
    /// `Bundle{}.with(Transform{...}).with(Velocity{...}).with(Health{...})`.
    /// Equivalent to writing `b.field = v; b.initialMask |= bit;` but
    /// declarative and harder to forget the mask half.
    ///
    /// @returns `*this` for chaining.
    template <typename T>
    Bundle& with(const T& v) noexcept;
};

namespace detail {

template <typename T>
constexpr Component bundleComponentBit() noexcept;

template <typename T>
constexpr void bundleStore(Bundle& b, const T& v) noexcept;

} // namespace detail

template <typename T>
inline Bundle& Bundle::with(const T& v) noexcept {
    detail::bundleStore(*this, v);
    initialMask |= ComponentSet{detail::bundleComponentBit<T>()};
    return *this;
}

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

/// §3.9.3 batch 18 — Heap-backed wrapper for the two oversize command
/// variants. Keeping `CmdSpawn` (248 B) and `CmdAddUserComponent`
/// (112 B) as direct variant alternatives padded every `Command`
/// instance to 256 B regardless of which variant it actually held, so
/// a `std::vector<Command>` of 100k value-only commands consumed
/// ~25 MB with ~80 % padding. Heap-allocating the two oversize
/// variants keeps the variant size at the inline-friendly 56 B
/// (dominated by `CmdSetTransform` at 48 B) — the four high-frequency
/// value setters live in place; spawn / addUserComponent pay one
/// allocation each but those are rare.
///
/// The heap-pointer field is the *only* difference. Hash bytes,
/// commit semantics, and visit dispatch are unchanged once the
/// pointer is dereferenced (see `EngineImpl::applyCommandImpl` /
/// `hashCommandImpl`).
using CmdSpawnPtr            = std::unique_ptr<CmdSpawn>;
using CmdAddUserComponentPtr = std::unique_ptr<CmdAddUserComponent>;

using Command = std::variant<
    CmdSpawnPtr,
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
    CmdRemoveTag,
    CmdAddUserComponentPtr,
    CmdRemoveUserComponent>;

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

    /// §3.10.3 batch 23 (F12) — bulk-spawn helper. Pairs each handle
    /// in @p reserved with the matching bundle in @p bundles and
    /// emits N `spawnBundle(reserved[i], bundles[i])` commands. The
    /// shorter of the two spans bounds the count, so callers can
    /// pass mismatched-length spans intentionally (the extra entries
    /// are silently skipped).
    ///
    /// Pre-reserves command-buffer storage to amortize the
    /// `emplace_back` churn under high-spawn workloads. Callers
    /// typically obtain @p reserved via
    /// `Engine::reserveEntityHandles(count, span)` (the batch
    /// reservation API). Combined with the §3.9.4 batch 19
    /// migration-batching hint inside `commitBuffer`, a bulk spawn
    /// pays roughly one geometric-growth event per destination
    /// chunk regardless of N.
    void spawnBundleN(std::span<const EntityHandle> reserved,
                      std::span<const Bundle> bundles);

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

    /// Generic per-component transition: write the value AND
    /// unconditionally attach the presence bit. Uniform across every
    /// built-in data component type.
    ///
    /// Unlike the per-type `setX` methods, this method ALWAYS attaches the
    /// bit, regardless of the value:
    /// `addComponent<RenderTag>(e, RenderTag{-1})` attaches the RenderTag
    /// bit even though `setRenderTag` would have cleared it. The semantic
    /// is "the entity logically carries T from now on"; opt out via
    /// @ref removeComponent.
    ///
    /// At commit time the engine migrates the entity into the
    /// destination archetype chunk (§3.1 batch 6): a swap-and-pop out
    /// of the source chunk plus a push into the destination chunk.
    /// Same-archetype writes (the bit was already set) skip the
    /// migration as a no-op fast path.
    ///
    /// Tag-only categories (`StaticTag`, `DisabledTag`, `DestroyedTag`)
    /// have no POD value — use @ref addTag for them. This method
    /// `static_assert`s for tag-only types.
    template <typename T>
    void addComponent(EntityHandle entity, const T& value);

    /// Detach component T from the entity by clearing its presence bit.
    /// At commit time the engine physically migrates the entity out of
    /// T's archetype chunk (§3.1 batch 6); a subsequent `tryGetT(e)`
    /// returns nullptr — the mask bit is the source of truth.
    ///
    /// For tag-only categories, use @ref removeTag — this method
    /// `static_assert`s for tag-only types.
    template <typename T>
    void removeComponent(EntityHandle entity);

    void reserve(std::size_t n)        { commands_.reserve(n); }
    void clear() noexcept              { commands_.clear(); valueOnlyCount_ = 0; }
    std::size_t size() const noexcept  { return commands_.size(); }
    bool empty() const noexcept        { return commands_.empty(); }

    /// §3.9.6 batch 21 — number of value-only commands recorded so far
    /// (`setTransform` / `setVelocity` / `setUserData` /
    /// `setAcceleration`). The commit phase uses this to short-circuit
    /// the sharded classifier when zero value-only commands are
    /// present (no parallelism possible) without a full pre-pass over
    /// the variant list.
    std::size_t valueOnlyCount() const noexcept { return valueOnlyCount_; }

    /// @internal Used internally by the commit phase.
    const std::vector<detail::Command>& commands() const noexcept { return commands_; }
    /// @internal Used internally by the commit phase.
    std::vector<detail::Command>& commands() noexcept { return commands_; }

private:
    std::vector<detail::Command> commands_;
    /// §3.9.6 batch 21 — bumped by the four value-only recording
    /// methods so the sharded commit can take a fast path without
    /// scanning the variant tag stream.
    std::size_t                  valueOnlyCount_ = 0;
};

template <typename T>
inline void CommandBuffer::addComponent(EntityHandle entity, const T& value) {
    if constexpr (std::is_same_v<T, Transform>)              setTransform(entity, value);
    else if constexpr (std::is_same_v<T, Velocity>)          setVelocity(entity, value);
    else if constexpr (std::is_same_v<T, RenderTag>)         setRenderTag(entity, value);
    else if constexpr (std::is_same_v<T, UserData>)          setUserData(entity, value);
    else if constexpr (std::is_same_v<T, Acceleration>)      setAcceleration(entity, value);
    else if constexpr (std::is_same_v<T, Parent>)            setParent(entity, value);
    else if constexpr (std::is_same_v<T, Health>)            setHealth(entity, value);
    else if constexpr (std::is_same_v<T, Faction>)           setFaction(entity, value);
    else if constexpr (std::is_same_v<T, AnimationStateRef>) setAnimationStateRef(entity, value);
    else if constexpr (std::is_same_v<T, PhysicsBodyRef>)    setPhysicsBodyRef(entity, value);
    else if constexpr (std::is_same_v<T, NavAgentRef>)       setNavAgentRef(entity, value);
    else if constexpr (std::is_same_v<T, BoundingVolume>)    setBoundingVolume(entity, value);
    else static_assert(sizeof(T) == 0,
        "CommandBuffer::addComponent: T must be a built-in data component "
        "(Transform, Velocity, RenderTag, UserData, Acceleration, Parent, "
        "Health, Faction, AnimationStateRef, PhysicsBodyRef, NavAgentRef, "
        "BoundingVolume). Tag-only categories go through addTag.");
    // Forcibly attach the presence bit. setX paths that already attach
    // it (Health, Faction, ...) make this a no-op; setX paths that
    // condition the bit on the value (RenderTag, Parent) get overridden
    // — addComponent always means "logically present".
    addTag(entity, detail::bundleComponentBit<T>());
}

template <typename T>
inline void CommandBuffer::removeComponent(EntityHandle entity) {
    if constexpr (std::is_same_v<T, Transform>
               || std::is_same_v<T, Velocity>
               || std::is_same_v<T, RenderTag>
               || std::is_same_v<T, UserData>
               || std::is_same_v<T, Acceleration>
               || std::is_same_v<T, Parent>
               || std::is_same_v<T, Health>
               || std::is_same_v<T, Faction>
               || std::is_same_v<T, AnimationStateRef>
               || std::is_same_v<T, PhysicsBodyRef>
               || std::is_same_v<T, NavAgentRef>
               || std::is_same_v<T, BoundingVolume>) {
        removeTag(entity, detail::bundleComponentBit<T>());
    } else {
        static_assert(sizeof(T) == 0,
            "CommandBuffer::removeComponent: T must be a built-in data "
            "component. Tag-only categories go through removeTag.");
    }
}

} // namespace threadmaxx
