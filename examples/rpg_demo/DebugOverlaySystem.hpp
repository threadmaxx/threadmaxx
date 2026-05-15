#pragma once

#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

namespace rpg {

/// Snapshots NPC positions + radii during `update` (parallel-safe read)
/// and emits debug-line circles for each NPC's AOI in
/// `buildRenderFrame`. Also draws a player-forward aim line.
class DebugOverlaySystem : public threadmaxx::ISystem {
public:
    DebugOverlaySystem(WorldState* worldState, UserComponentIds* ids)
        : worldState_(worldState), ids_(ids) {}

    const char* name() const noexcept override { return "debug-overlay"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }

    void update(threadmaxx::SystemContext& ctx) override;
    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override;

private:
    struct NpcDebug {
        threadmaxx::Vec3 position;
        float            radius;
        std::uint32_t    color;
    };

    WorldState*       worldState_ = nullptr;
    UserComponentIds* ids_        = nullptr;
    std::vector<NpcDebug> npcs_;
    threadmaxx::Vec3      playerPos_{0, 1, 0};
    float                 playerYaw_ = 0.0f;
    bool                  havePlayer_ = false;
};

} // namespace rpg
