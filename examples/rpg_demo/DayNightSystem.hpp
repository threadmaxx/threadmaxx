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
    /// §3.11.5 batch D5 — cosmetic, safe to skip under tick-budget
    /// pressure. The sun angle stalls for a tick or two; lighting
    /// only freezes visibly if many consecutive skips happen.
    bool skippable() const noexcept override { return true; }
    void update(threadmaxx::SystemContext&) override {}
    void postStep(threadmaxx::SystemContext& ctx) override;
    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override;

private:
    WorldState* worldState_ = nullptr;
};

} // namespace rpg
