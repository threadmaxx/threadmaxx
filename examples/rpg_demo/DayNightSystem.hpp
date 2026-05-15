#pragma once

#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

namespace rpg {

/// Day/night cycle. `postStep` advances the sun angle stored on
/// `WorldState`; `buildRenderFrame` pushes one directional `Light`
/// with the matching direction + warm/cool color tint. Pure
/// game-side — the renderer just consumes the Light POD.
class DayNightSystem : public threadmaxx::ISystem {
public:
    explicit DayNightSystem(WorldState* worldState) : worldState_(worldState) {}

    const char* name() const noexcept override { return "day-night"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
    void update(threadmaxx::SystemContext&) override {}
    void postStep(threadmaxx::SystemContext& ctx) override;
    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override;

private:
    WorldState* worldState_ = nullptr;
};

} // namespace rpg
