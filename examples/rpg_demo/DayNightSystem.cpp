#include "DayNightSystem.hpp"

#include <threadmaxx/render/RenderFrameBuilder.hpp>

#include <algorithm>
#include <cmath>

namespace rpg {

namespace {

// 2026-05-22 audit (round 4) — sky sphere radius for the sun + moon
// DrawItems. Large enough that the disc is visible from any spot on
// the 256 m terrain regardless of the player's XZ — the items live
// in world space at `(±cos(a), ±sin(a), 0) * skyDist`, so a player
// at the far corner still sees a sun roughly in the right direction
// (parallax is acceptable for cosmetic light bodies).
constexpr float kSkyDistance      = 250.0f;
constexpr float kSunScale         =  10.0f;
constexpr float kMoonScale        =   7.0f;
/// Day light when the sun is at zenith.
constexpr float kDayMaxIntensity  = 1.35f;
/// Light at sunrise/sunset (sin(a)=0): the day branch's floor.
constexpr float kDawnIntensity    = 0.25f;
/// Moonlight intensity — same value across the whole night so we
/// don't pulse on the symmetric a=3π/2 angle.
constexpr float kNightIntensity   = 0.15f;

constexpr threadmaxx::Vec3 kNightColor   = { 0.55f, 0.65f, 0.85f };
constexpr threadmaxx::Vec3 kSunsetColor  = { 1.0f,  0.55f, 0.30f };
constexpr threadmaxx::Vec3 kNoonColor    = { 1.0f,  0.95f, 0.85f };

constexpr threadmaxx::Vec3 kSunMaterial  = { 1.0f,  0.90f, 0.30f };
constexpr threadmaxx::Vec3 kMoonMaterial = { 0.90f, 0.92f, 1.0f  };

} // namespace

void DayNightSystem::postStep(threadmaxx::SystemContext& ctx) {
    const float dt = static_cast<float>(ctx.dt());
    const float twoPi = 6.2831853f;
    worldState_->sunAngle += twoPi * dt / std::max(worldState_->dayLengthSeconds, 1.0f);
    if (worldState_->sunAngle > twoPi) worldState_->sunAngle -= twoPi;
}

void DayNightSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    const float a    = worldState_->sunAngle;
    const float sinA = std::sin(a);
    const float cosA = std::cos(a);

    // 2026-05-22 audit (round 4) — split day vs night light branches.
    // The round-3 form used `-abs(sin(a))` which kept the night
    // identical to noon (just shadows from the wrong side) and felt
    // like "lit moon surface" instead of nighttime. We now make
    // night meaningfully darker (`kNightIntensity` = 0.15, cool
    // blue) and let the day band go up to ~1.35 at noon — a 9×
    // intensity swing that reads on screen.
    threadmaxx::Light l = {};
    l.type        = threadmaxx::LightType::Directional;
    l.castsShadow = false;

    if (sinA > 0.0f) {
        // Day: sun position = (cos(a), sin(a), 0) * dist. Light
        // direction is the *flow* (sun → ground) = -sunPos / dist.
        // We add a small Z component so faces aligned with X also
        // catch light (looks less flat than a strict X/Y direction).
        l.direction = { -cosA, -std::max(0.05f, sinA), -0.20f };

        const float t = std::clamp(sinA, 0.0f, 1.0f);  // 0 at horizon, 1 at noon
        l.color = {
            kSunsetColor.x * (1.0f - t) + kNoonColor.x * t,
            kSunsetColor.y * (1.0f - t) + kNoonColor.y * t,
            kSunsetColor.z * (1.0f - t) + kNoonColor.z * t,
        };
        l.intensity = kDawnIntensity + (kDayMaxIntensity - kDawnIntensity) * t;
    } else {
        // Night: moon at opposite of sun position. Light direction
        // points downward from where the moon is.
        l.direction = { cosA, std::max(0.05f, -sinA), 0.20f };
        l.color     = kNightColor;
        l.intensity = kNightIntensity;
    }
    b.addLight(l);

    // ---- Sun and moon DrawItems ------------------------------------------
    //
    // 2026-05-22 audit (round 4) — first-time sky bodies. The day
    // body sits at (cos(a), sin(a), 0) * skyDist; the night body
    // sits at the opposite. We draw whichever is above the horizon
    // — at the exact horizon crossing (sin(a)=0) the disc would be
    // glued to the ground line, so we skip the band where it'd
    // intersect the terrain (~ |sin(a)| < 0.04).
    constexpr float kHorizonHide = 0.04f;
    if (sinA > kHorizonHide) {
        threadmaxx::DrawItem sun = {};
        sun.transform.position    = { cosA * kSkyDistance,
                                      sinA * kSkyDistance + 30.0f,
                                      0.0f };
        sun.transform.scale       = { kSunScale, kSunScale, kSunScale };
        sun.transform.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
        sun.meshId                = 0;   // default cube
        sun.materialId            = 0;
        sun.materialOverride.params = {
            kSunMaterial.x, kSunMaterial.y, kSunMaterial.z, 1.0f,
        };
        sun.cameraMask = ~0u;
        b.addDrawItem(threadmaxx::RenderPass::Opaque, sun);
    } else if (sinA < -kHorizonHide) {
        threadmaxx::DrawItem moon = {};
        moon.transform.position    = { -cosA * kSkyDistance,
                                       -sinA * kSkyDistance + 30.0f,
                                       0.0f };
        moon.transform.scale       = { kMoonScale, kMoonScale, kMoonScale };
        moon.transform.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
        moon.meshId                = 0;
        moon.materialId            = 0;
        moon.materialOverride.params = {
            kMoonMaterial.x, kMoonMaterial.y, kMoonMaterial.z, 1.0f,
        };
        moon.cameraMask = ~0u;
        b.addDrawItem(threadmaxx::RenderPass::Opaque, moon);
    }
}

} // namespace rpg
