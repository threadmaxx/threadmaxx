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

// SHARDED_OPTIMISATION.md S8 — recording-side router for value-only
// commands. When a locator is installed, query the destination
// archetype and push the command index into the matching bucket;
// stale handles fall through to the global lane so commit can apply
// them via the no-op-on-stale `mut*()` setters.
void CommandBuffer::routeValueOnly(EntityHandle e,
                                   std::uint32_t cmdIdx) noexcept {
    if (!locatorFn_) return;
    const std::uint32_t arch = locatorFn_(locatorCtx_, e);
    if (arch == kInvalidArchetype) {
        globalIdx_.push_back(cmdIdx);
        return;
    }
    if (arch >= chunkBuckets_.size()) {
        chunkBuckets_.resize(static_cast<std::size_t>(arch) + 1);
    }
    chunkBuckets_[arch].push_back(cmdIdx);
}

void CommandBuffer::routeGlobal(std::uint32_t cmdIdx) noexcept {
    if (!locatorFn_) return;
    globalIdx_.push_back(cmdIdx);
}

void CommandBuffer::clear() noexcept {
    commands_.clear();
    valueOnlyCount_ = 0;
    globalIdx_.clear();
    for (auto& b : chunkBuckets_) b.clear();
    // `locatorFn_` / `locatorCtx_` persist across clear() — the engine
    // re-installs them per wave.
}

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
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
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
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
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
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
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
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
}
void CommandBuffer::spawnBundle(const Bundle& b) {
    commands_.emplace_back(std::make_unique<detail::CmdSpawn>(
        detail::CmdSpawn{
            b.transform, b.velocity, b.renderTag, b.userData,
            b.acceleration, b.parent,
            b.health, b.faction, b.animationState,
            b.physicsBody, b.navAgent, b.boundingVolume,
            b.initialMask, kInvalidEntity}));
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
}
void CommandBuffer::spawnBundle(EntityHandle reserved, const Bundle& b) {
    commands_.emplace_back(std::make_unique<detail::CmdSpawn>(
        detail::CmdSpawn{
            b.transform, b.velocity, b.renderTag, b.userData,
            b.acceleration, b.parent,
            b.health, b.faction, b.animationState,
            b.physicsBody, b.navAgent, b.boundingVolume,
            b.initialMask, reserved}));
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
}
// §3.10.3 batch 23 (F12) — bulk spawn. Pre-reserve the storage so
// the loop's per-spawn `emplace_back` doesn't trigger geometric
// vector growth N times. Loop body is one `make_unique<CmdSpawn>` +
// one `commands_.emplace_back`; the §3.9.4 batch 19 migration-batch
// hint inside `EngineImpl::commitBuffer` handles the destination-
// chunk amortization on the apply side.
void CommandBuffer::spawnBundleN(std::span<const EntityHandle> reserved,
                                 std::span<const Bundle> bundles) {
    const std::size_t n = std::min(reserved.size(), bundles.size());
    if (n == 0) return;
    commands_.reserve(commands_.size() + n);
    if (locatorFn_) globalIdx_.reserve(globalIdx_.size() + n);
    for (std::size_t i = 0; i < n; ++i) {
        spawnBundle(reserved[i], bundles[i]);
    }
}
void CommandBuffer::destroy(EntityHandle e) {
    commands_.emplace_back(detail::CmdDestroy{e});
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
}
void CommandBuffer::setTransform(EntityHandle e, const Transform& t) {
    commands_.emplace_back(detail::CmdSetTransform{e, t});
    ++valueOnlyCount_;
    routeValueOnly(e, static_cast<std::uint32_t>(commands_.size() - 1));
}
void CommandBuffer::setVelocity(EntityHandle e, const Velocity& v) {
    commands_.emplace_back(detail::CmdSetVelocity{e, v});
    ++valueOnlyCount_;
    routeValueOnly(e, static_cast<std::uint32_t>(commands_.size() - 1));
}
void CommandBuffer::setRenderTag(EntityHandle e, const RenderTag& r) {
    commands_.emplace_back(detail::CmdSetRenderTag{e, r});
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
}
void CommandBuffer::setUserData(EntityHandle e, const UserData& u) {
    commands_.emplace_back(detail::CmdSetUserData{e, u});
    ++valueOnlyCount_;
    routeValueOnly(e, static_cast<std::uint32_t>(commands_.size() - 1));
}
void CommandBuffer::setAcceleration(EntityHandle e, const Acceleration& a) {
    commands_.emplace_back(detail::CmdSetAcceleration{e, a});
    ++valueOnlyCount_;
    routeValueOnly(e, static_cast<std::uint32_t>(commands_.size() - 1));
}
void CommandBuffer::setParent(EntityHandle e, const Parent& p) {
    commands_.emplace_back(detail::CmdSetParent{e, p});
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
}
void CommandBuffer::setHealth(EntityHandle e, const Health& h) {
    commands_.emplace_back(detail::CmdSetHealth{e, h});
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
}
void CommandBuffer::setFaction(EntityHandle e, const Faction& f) {
    commands_.emplace_back(detail::CmdSetFaction{e, f});
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
}
void CommandBuffer::setAnimationStateRef(EntityHandle e, const AnimationStateRef& a) {
    commands_.emplace_back(detail::CmdSetAnimationState{e, a});
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
}
void CommandBuffer::setPhysicsBodyRef(EntityHandle e, const PhysicsBodyRef& p) {
    commands_.emplace_back(detail::CmdSetPhysicsBody{e, p});
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
}
void CommandBuffer::setNavAgentRef(EntityHandle e, const NavAgentRef& n) {
    commands_.emplace_back(detail::CmdSetNavAgent{e, n});
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
}
void CommandBuffer::setBoundingVolume(EntityHandle e, const BoundingVolume& b) {
    commands_.emplace_back(detail::CmdSetBoundingVolume{e, b});
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
}
void CommandBuffer::setComponentMask(EntityHandle e, ComponentSet m) {
    commands_.emplace_back(detail::CmdSetComponentMask{e, m});
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
}
void CommandBuffer::addTag(EntityHandle e, Component tag) {
    commands_.emplace_back(detail::CmdAddTag{e, tag});
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
}
void CommandBuffer::removeTag(EntityHandle e, Component tag) {
    commands_.emplace_back(detail::CmdRemoveTag{e, tag});
    routeGlobal(static_cast<std::uint32_t>(commands_.size() - 1));
}

} // namespace threadmaxx
