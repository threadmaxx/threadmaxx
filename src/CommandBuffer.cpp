/// @file CommandBuffer.cpp
/// Recorder-side implementation of the public CommandBuffer API.
///
/// Every method here appends a `detail::Command` variant to the
/// per-job command vector — no locks, no allocation contention. The
/// commit phase in `EngineImpl::commitBuffer` is the consumer: each
/// new variant added here must get a corresponding case in the
/// `std::visit` lambda there, or it'll silently drop on commit.
#include "threadmaxx/CommandBuffer.hpp"

namespace threadmaxx {

namespace {

// Derive the default per-entity component mask from spawn values. Every
// built-in component except RenderTag and Parent is unconditionally
// present; RenderTag is set iff the caller supplied a renderable mesh,
// Parent is set iff the caller supplied a valid parent handle. The
// explicit-mask overloads bypass this entirely.
//
// The §3.1 batch-5 slots (Health, Faction, AnimationStateRef,
// PhysicsBodyRef, NavAgentRef, BoundingVolume) are NOT auto-derived
// here: gameplay code must opt in via an explicit mask, a Bundle, or
// the per-component setters. This keeps existing call sites unchanged
// — pre-batch-5 spawns still produce pre-batch-5 entities.
ComponentSet defaultSpawnMask(const RenderTag& r, const Parent& p) noexcept {
    ComponentSet m{Component::Transform};
    m |= ComponentSet{Component::Velocity};
    m |= ComponentSet{Component::UserData};
    m |= ComponentSet{Component::Acceleration};
    if (r.meshId >= 0)    m |= ComponentSet{Component::RenderTag};
    if (p.parent.valid()) m |= ComponentSet{Component::Parent};
    return m;
}

} // namespace

// §3.9.3 batch 18 — heap-allocate `CmdSpawn`. Saves ~200 B / command
// in the `std::vector<Command>` storage; spawn itself is rare so the
// extra allocation per call is a clean trade.
void CommandBuffer::spawn(const Transform& t, const Velocity& v,
                          const RenderTag& r, const UserData& u,
                          const Acceleration& a, const Parent& p) {
    commands_.emplace_back(std::make_unique<detail::CmdSpawn>(
        detail::CmdSpawn{t, v, r, u, a, p,
                         Health{}, Faction{},
                         AnimationStateRef{},
                         PhysicsBodyRef{}, NavAgentRef{},
                         BoundingVolume{},
                         defaultSpawnMask(r, p),
                         kInvalidEntity}));
}
void CommandBuffer::spawn(const Transform& t, const Velocity& v,
                          const RenderTag& r, const UserData& u,
                          const Acceleration& a,
                          const Parent& p, ComponentSet initialMask) {
    commands_.emplace_back(std::make_unique<detail::CmdSpawn>(
        detail::CmdSpawn{t, v, r, u, a, p,
                         Health{}, Faction{},
                         AnimationStateRef{},
                         PhysicsBodyRef{}, NavAgentRef{},
                         BoundingVolume{},
                         initialMask,
                         kInvalidEntity}));
}
void CommandBuffer::spawn(EntityHandle reserved,
                          const Transform& t, const Velocity& v,
                          const RenderTag& r, const UserData& u,
                          const Acceleration& a, const Parent& p) {
    commands_.emplace_back(std::make_unique<detail::CmdSpawn>(
        detail::CmdSpawn{t, v, r, u, a, p,
                         Health{}, Faction{},
                         AnimationStateRef{},
                         PhysicsBodyRef{}, NavAgentRef{},
                         BoundingVolume{},
                         defaultSpawnMask(r, p),
                         reserved}));
}
void CommandBuffer::spawn(EntityHandle reserved,
                          const Transform& t, const Velocity& v,
                          const RenderTag& r, const UserData& u,
                          const Acceleration& a,
                          const Parent& p, ComponentSet initialMask) {
    commands_.emplace_back(std::make_unique<detail::CmdSpawn>(
        detail::CmdSpawn{t, v, r, u, a, p,
                         Health{}, Faction{},
                         AnimationStateRef{},
                         PhysicsBodyRef{}, NavAgentRef{},
                         BoundingVolume{},
                         initialMask,
                         reserved}));
}
void CommandBuffer::spawnBundle(const Bundle& b) {
    commands_.emplace_back(std::make_unique<detail::CmdSpawn>(
        detail::CmdSpawn{
            b.transform, b.velocity, b.renderTag, b.userData,
            b.acceleration, b.parent,
            b.health, b.faction, b.animationState,
            b.physicsBody, b.navAgent, b.boundingVolume,
            b.initialMask, kInvalidEntity}));
}
void CommandBuffer::spawnBundle(EntityHandle reserved, const Bundle& b) {
    commands_.emplace_back(std::make_unique<detail::CmdSpawn>(
        detail::CmdSpawn{
            b.transform, b.velocity, b.renderTag, b.userData,
            b.acceleration, b.parent,
            b.health, b.faction, b.animationState,
            b.physicsBody, b.navAgent, b.boundingVolume,
            b.initialMask, reserved}));
}
void CommandBuffer::destroy(EntityHandle e) {
    commands_.emplace_back(detail::CmdDestroy{e});
}
void CommandBuffer::setTransform(EntityHandle e, const Transform& t) {
    commands_.emplace_back(detail::CmdSetTransform{e, t});
}
void CommandBuffer::setVelocity(EntityHandle e, const Velocity& v) {
    commands_.emplace_back(detail::CmdSetVelocity{e, v});
}
void CommandBuffer::setRenderTag(EntityHandle e, const RenderTag& r) {
    commands_.emplace_back(detail::CmdSetRenderTag{e, r});
}
void CommandBuffer::setUserData(EntityHandle e, const UserData& u) {
    commands_.emplace_back(detail::CmdSetUserData{e, u});
}
void CommandBuffer::setAcceleration(EntityHandle e, const Acceleration& a) {
    commands_.emplace_back(detail::CmdSetAcceleration{e, a});
}
void CommandBuffer::setParent(EntityHandle e, const Parent& p) {
    commands_.emplace_back(detail::CmdSetParent{e, p});
}
void CommandBuffer::setHealth(EntityHandle e, const Health& h) {
    commands_.emplace_back(detail::CmdSetHealth{e, h});
}
void CommandBuffer::setFaction(EntityHandle e, const Faction& f) {
    commands_.emplace_back(detail::CmdSetFaction{e, f});
}
void CommandBuffer::setAnimationStateRef(EntityHandle e, const AnimationStateRef& a) {
    commands_.emplace_back(detail::CmdSetAnimationState{e, a});
}
void CommandBuffer::setPhysicsBodyRef(EntityHandle e, const PhysicsBodyRef& p) {
    commands_.emplace_back(detail::CmdSetPhysicsBody{e, p});
}
void CommandBuffer::setNavAgentRef(EntityHandle e, const NavAgentRef& n) {
    commands_.emplace_back(detail::CmdSetNavAgent{e, n});
}
void CommandBuffer::setBoundingVolume(EntityHandle e, const BoundingVolume& b) {
    commands_.emplace_back(detail::CmdSetBoundingVolume{e, b});
}
void CommandBuffer::setComponentMask(EntityHandle e, ComponentSet m) {
    commands_.emplace_back(detail::CmdSetComponentMask{e, m});
}
void CommandBuffer::addTag(EntityHandle e, Component tag) {
    commands_.emplace_back(detail::CmdAddTag{e, tag});
}
void CommandBuffer::removeTag(EntityHandle e, Component tag) {
    commands_.emplace_back(detail::CmdRemoveTag{e, tag});
}

} // namespace threadmaxx
