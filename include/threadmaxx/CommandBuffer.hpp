#pragma once

#include "Components.hpp"
#include "Handles.hpp"

#include <variant>
#include <vector>

namespace threadmaxx {

namespace detail {

struct CmdSpawn {
    Transform transform;
    Velocity velocity;
    RenderTag render;
    UserData userData;
    // Set by the engine during commit so callers (if needed) can be told what
    // handle was assigned. Not used during recording.
    EntityHandle* outHandle = nullptr;
};
struct CmdDestroy {
    EntityHandle entity;
};
struct CmdSetTransform {
    EntityHandle entity;
    Transform value;
};
struct CmdSetVelocity {
    EntityHandle entity;
    Velocity value;
};
struct CmdSetRenderTag {
    EntityHandle entity;
    RenderTag value;
};
struct CmdSetUserData {
    EntityHandle entity;
    UserData value;
};

using Command = std::variant<
    CmdSpawn,
    CmdDestroy,
    CmdSetTransform,
    CmdSetVelocity,
    CmdSetRenderTag,
    CmdSetUserData>;

} // namespace detail

// CommandBuffer is per-job: a worker thread owns one for the duration of its
// job and records mutations into it. The engine collects all command buffers
// after the batch completes and applies them in deterministic order on the
// simulation thread. There are no locks here on purpose.
class CommandBuffer {
public:
    CommandBuffer() = default;
    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;
    CommandBuffer(CommandBuffer&&) noexcept = default;
    CommandBuffer& operator=(CommandBuffer&&) noexcept = default;

    void spawn(const Transform& t, const Velocity& v = {},
               const RenderTag& r = {}, const UserData& u = {});
    void destroy(EntityHandle entity);
    void setTransform(EntityHandle entity, const Transform& t);
    void setVelocity(EntityHandle entity, const Velocity& v);
    void setRenderTag(EntityHandle entity, const RenderTag& r);
    void setUserData(EntityHandle entity, const UserData& u);

    void reserve(std::size_t n) { commands_.reserve(n); }
    void clear() noexcept { commands_.clear(); }
    std::size_t size() const noexcept { return commands_.size(); }
    bool empty() const noexcept { return commands_.empty(); }

    // Used internally by the commit phase.
    const std::vector<detail::Command>& commands() const noexcept { return commands_; }
    std::vector<detail::Command>& commands() noexcept { return commands_; }

private:
    std::vector<detail::Command> commands_;
};

} // namespace threadmaxx
