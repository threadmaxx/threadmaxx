#include "ParticleSystem.hpp"

#include <threadmaxx/System.hpp>
#include <threadmaxx/render/DebugGeometry.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>

#include <algorithm>
#include <cmath>

namespace tou2d {

namespace {

// Per-kind motion params (world units per tick at 60 Hz; scaled by dt
// internally so the integration stays correct under variable step).
constexpr float kDebrisGravity = 220.0f;   // wu / s², downward
constexpr float kSmokeGravity  = -45.0f;   // wu / s², upward (smoke rises)
constexpr float kSparkGravity  = 0.0f;

// Alpha decay envelope. exponent > 1 → "stays bright then crashes"
// (debris); exponent < 1 → "smooth long fade" (smoke); exponent >> 1
// → "bright flash then gone" (spark).
constexpr float kDebrisAlphaExp = 1.4f;
constexpr float kSmokeAlphaExp  = 0.85f;
constexpr float kSparkAlphaExp  = 2.0f;

// Pixel-size shrink floor — particles never quite reach 0 px so the
// final frame isn't invisible.
constexpr float kPxShrinkMin = 0.6f;

// Smoke drag coefficient (per second). Slows the puff as it ages.
constexpr float kSmokeDrag = 1.2f;

constexpr float kTwoPi = 6.28318530718f;

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

} // namespace

void ParticleSystem::update(threadmaxx::SystemContext& ctx) {
    const float dt = static_cast<float>(ctx.dt());
    for (auto& p : pool_) {
        if (p.ttlTicks == 0) continue;
        // Gravity. Down is -Y in this game's world frame (see
        // MovementSystem's `v.linear.y -= kGravityAccel * dt`), so we
        // subtract a POSITIVE gravity to fall and add for smoke-rises.
        const float g =
            p.kind == Kind::Smoke  ? kSmokeGravity
          : p.kind == Kind::Spark  ? kSparkGravity
                                   : kDebrisGravity;
        p.vy -= g * dt;
        p.x  += p.vx * dt;
        p.y  += p.vy * dt;
        if (p.kind == Kind::Smoke) {
            const float drag = std::exp(-kSmokeDrag * dt);
            p.vx *= drag;
            p.vy *= drag;
        }
        p.ttlTicks = static_cast<std::uint16_t>(p.ttlTicks - 1);
    }
}

void ParticleSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    for (const auto& p : pool_) {
        if (p.ttlTicks == 0) continue;
        const float frac =
            static_cast<float>(p.ttlTicks) /
            static_cast<float>(p.maxTtl);
        const float exponent =
            p.kind == Kind::Smoke  ? kSmokeAlphaExp
          : p.kind == Kind::Spark  ? kSparkAlphaExp
                                   : kDebrisAlphaExp;
        // M6.7 — photosensitive cap. Strictly render-side: pool_ /
        // ttlTicks / rgb are unchanged, so commitHash is unaffected.
        const float alphaCap =
            access_.photosensitive ? kPhotosensitiveAlphaScale : 1.0f;
        const float alpha = std::pow(frac, exponent) * alphaCap;
        const std::uint32_t a8 = static_cast<std::uint32_t>(
            std::clamp(alpha * 255.0f, 0.0f, 255.0f));

        threadmaxx::DebugPoint pt{};
        pt.position  = { p.x, p.y, 0.0f };
        pt.colorRGBA = (p.rgb & 0x00FFFFFFu) | (a8 << 24);
        pt.pixelSize = lerp(kPxShrinkMin, p.pxSize, frac);
        b.addDebugPoint(pt);
    }
}

void ParticleSystem::spawn(const Particle& p) {
    pool_[head_] = p;
    head_ = (head_ + 1u) % static_cast<std::uint32_t>(kMaxParticles);
}

void ParticleSystem::emitDeathExplosion(float x, float y, std::uint32_t color) {
    std::uniform_real_distribution<float> angDist  (0.0f, kTwoPi);
    std::uniform_real_distribution<float> spDebris (60.0f, 180.0f);
    std::uniform_real_distribution<float> spSmoke  (20.0f, 70.0f);
    std::uniform_int_distribution<int>    ttlDebris(35, 55);
    std::uniform_int_distribution<int>    ttlSmoke (60, 90);

    // 16 ship-colored debris fragments.
    for (int i = 0; i < 16; ++i) {
        const float a = angDist(rng_);
        const float s = spDebris(rng_);
        Particle p{};
        p.x = x;  p.y = y;
        p.vx = std::cos(a) * s;
        p.vy = std::sin(a) * s;
        p.rgb = color & 0x00FFFFFFu;
        const int ttl = ttlDebris(rng_);
        p.ttlTicks = static_cast<std::uint16_t>(ttl);
        p.maxTtl   = static_cast<std::uint16_t>(ttl);
        p.pxSize   = 5.5f;
        p.kind     = Kind::Debris;
        spawn(p);
    }
    // 10 dark smoke puffs. Initial vy nudged upward so the puff drifts
    // skyward regardless of explosion ring orientation.
    for (int i = 0; i < 10; ++i) {
        const float a = angDist(rng_);
        const float s = spSmoke(rng_);
        Particle p{};
        p.x = x;  p.y = y;
        p.vx = std::cos(a) * s;
        p.vy = std::sin(a) * s + 30.0f;
        p.rgb = 0x00606468u;  // dark-gray smoke
        const int ttl = ttlSmoke(rng_);
        p.ttlTicks = static_cast<std::uint16_t>(ttl);
        p.maxTtl   = static_cast<std::uint16_t>(ttl);
        p.pxSize   = 7.5f;
        p.kind     = Kind::Smoke;
        spawn(p);
    }
}

void ParticleSystem::emitImpactSpark(float x, float y, std::uint32_t color) {
    std::uniform_real_distribution<float> angDist (0.0f, kTwoPi);
    std::uniform_real_distribution<float> spDist  (40.0f, 110.0f);
    std::uniform_int_distribution<int>    ttlDist (8, 16);
    for (int i = 0; i < 5; ++i) {
        const float a = angDist(rng_);
        const float s = spDist(rng_);
        Particle p{};
        p.x = x;  p.y = y;
        p.vx = std::cos(a) * s;
        p.vy = std::sin(a) * s;
        p.rgb = color & 0x00FFFFFFu;
        const int ttl = ttlDist(rng_);
        p.ttlTicks = static_cast<std::uint16_t>(ttl);
        p.maxTtl   = static_cast<std::uint16_t>(ttl);
        p.pxSize   = 4.0f;
        p.kind     = Kind::Spark;
        spawn(p);
    }
}

void ParticleSystem::emitTileBreakDust(float x, float y) {
    std::uniform_real_distribution<float> angDist (0.0f, kTwoPi);
    std::uniform_real_distribution<float> spDist  (20.0f, 70.0f);
    std::uniform_int_distribution<int>    ttlDist (18, 28);
    for (int i = 0; i < 6; ++i) {
        const float a = angDist(rng_);
        const float s = spDist(rng_);
        Particle p{};
        p.x = x;  p.y = y;
        p.vx = std::cos(a) * s;
        p.vy = std::sin(a) * s + 25.0f;
        p.rgb = 0x00584034u;  // dirt brown
        const int ttl = ttlDist(rng_);
        p.ttlTicks = static_cast<std::uint16_t>(ttl);
        p.maxTtl   = static_cast<std::uint16_t>(ttl);
        p.pxSize   = 4.5f;
        p.kind     = Kind::Debris;
        spawn(p);
    }
}

} // namespace tou2d
