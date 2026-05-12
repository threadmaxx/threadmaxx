#include "threadmaxx/CommandBuffer.hpp"

namespace threadmaxx {

void CommandBuffer::spawn(const Transform& t, const Velocity& v,
                          const RenderTag& r, const UserData& u) {
    commands_.emplace_back(detail::CmdSpawn{t, v, r, u, nullptr});
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

} // namespace threadmaxx
