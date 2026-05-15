#include "DayNightSystem.hpp"

#include <threadmaxx/render/RenderFrameBuilder.hpp>

#include <algorithm>
#include <cmath>

namespace rpg {

void DayNightSystem::postStep(threadmaxx::SystemContext& ctx) {
    const float dt = static_cast<float>(ctx.dt());
    const float twoPi = 6.2831853f;
    worldState_->sunAngle += twoPi * dt / std::max(worldState_->dayLengthSeconds, 1.0f);
    if (worldState_->sunAngle > twoPi) worldState_->sunAngle -= twoPi;
}

void DayNightSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    const float a = worldState_->sunAngle;
    // Sun direction sweeps the YZ plane: noon → straight down; midnight
    // → straight up. Y is up.
    const threadmaxx::Vec3 dir{
        std::cos(a) * 0.3f,
        -std::abs(std::sin(a)),     // never below horizon (no night fade-to-black)
        std::sin(a) * 0.7f,
    };

    // Color: warmer at low sun, cooler at high sun. Mix between sunset
    // orange and noon white based on |sin(angle)|.
    const float t = std::clamp(std::abs(std::sin(a)), 0.0f, 1.0f);
    threadmaxx::Vec3 color{
        1.0f * (1.0f - t) + 1.0f * t,
        0.55f * (1.0f - t) + 0.95f * t,
        0.30f * (1.0f - t) + 0.85f * t,
    };

    threadmaxx::Light l = {};
    l.type        = threadmaxx::LightType::Directional;
    l.direction   = dir;
    l.color       = color;
    l.intensity   = 0.85f + 0.5f * t;
    l.castsShadow = false;
    b.addLight(l);
}

} // namespace rpg
