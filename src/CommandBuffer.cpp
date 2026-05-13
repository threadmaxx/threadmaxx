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
// built-in component except RenderTag and Parent is unconditionally present;
// RenderTag is present iff the caller supplied a renderable mesh; Parent is
// off by default (the no-parent spawn overload never attaches one).
ComponentSet defaultSpawnMask(const RenderTag& r) noexcept {
    ComponentSet m{Component::Transform};
    m |= ComponentSet{Component::Velocity};
    m |= ComponentSet{Component::UserData};
    m |= ComponentSet{Component::Acceleration};
    if (r.meshId >= 0) m |= ComponentSet{Component::RenderTag};
    return m;
}

} // namespace

void CommandBuffer::spawn(const Transform& t, const Velocity& v,
                          const RenderTag& r, const UserData& u,
                          const Acceleration& a) {
    commands_.emplace_back(detail::CmdSpawn{t, v, r, u, a, Parent{},
                                            defaultSpawnMask(r), nullptr});
}
void CommandBuffer::spawn(const Transform& t, const Velocity& v,
                          const RenderTag& r, const UserData& u,
                          const Acceleration& a,
                          const Parent& p, ComponentSet initialMask) {
    commands_.emplace_back(detail::CmdSpawn{t, v, r, u, a, p, initialMask, nullptr});
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
void CommandBuffer::setComponentMask(EntityHandle e, ComponentSet m) {
    commands_.emplace_back(detail::CmdSetComponentMask{e, m});
}

} // namespace threadmaxx
