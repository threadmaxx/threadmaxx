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
constexpr float kThrustGravity = 0.0f;     // M7.3 — plume floats free

// Alpha decay envelope. exponent > 1 → "stays bright then crashes"
// (debris); exponent < 1 → "smooth long fade" (smoke); exponent >> 1
// → "bright flash then gone" (spark).
constexpr float kDebrisAlphaExp = 1.4f;
constexpr float kSmokeAlphaExp  = 0.85f;
constexpr float kSparkAlphaExp  = 2.0f;
constexpr float kThrustAlphaExp = 1.6f;    // M7.3 — fades faster than debris

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
          : p.kind == Kind::Thrust ? kThrustGravity
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
          : p.kind == Kind::Thrust ? kThrustAlphaExp
                                   : kDebrisAlphaExp;
        // M6.7 — photosensitive cap. Strictly render-side: pool_ /
        // ttlTicks / rgb are unchanged, so commitHash is unaffected.
        const float alphaCap =
            access_.photosensitive ? kPhotosensitiveAlphaScale : 1.0f;
        const float alpha = std::pow(frac, exponent) * alphaCap;
        const std::uint32_t a8 = static_cast<std::uint32_t>(
            std::clamp(alpha * 255.0f, 0.0f, 255.0f));

        // M7.3 §5.1 — thruster particles override stored rgb with an
        // age-keyed yellow→red lerp; every other kind uses the stored
        // rgb verbatim (set at emit time).
        const std::uint32_t rgb24 = (p.kind == Kind::Thrust)
            ? (thrustColorForAge(frac) & 0x00FFFFFFu)
            : (p.rgb & 0x00FFFFFFu);

        threadmaxx::DebugPoint pt{};
        pt.position  = { p.x, p.y, 0.0f };
        pt.colorRGBA = rgb24 | (a8 << 24);
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

void ParticleSystem::emitThrusterParticle(float x, float y, float vx, float vy) {
    // M7.3 — single puff per call. TTL is short (12-18 ticks ≈ 0.2-0.3 s
    // at 60 Hz) so the trail length stays bounded even with continuous
    // thrust; small `pxSize` so the trail reads as a wisp rather than
    // a chunky debris stream. RNG seeded from rng_ (FEEDBEEF) → same
    // sequence across runs given the same call order.
    std::uniform_int_distribution<int> ttlDist(12, 18);
    Particle p{};
    p.x = x;  p.y = y;
    p.vx = vx; p.vy = vy;
    p.rgb = kThrustColorHot & 0x00FFFFFFu;
    const int ttl = ttlDist(rng_);
    p.ttlTicks = static_cast<std::uint16_t>(ttl);
    p.maxTtl   = static_cast<std::uint16_t>(ttl);
    p.pxSize   = 3.5f;
    p.kind     = Kind::Thrust;
    spawn(p);
}

void ParticleSystem::emitDamageSmoke(float x, float y) {
    // M7.3 — single dark-gray smoke puff at the ship's pos. Shorter
    // TTL than the death-explosion smoke (30-50 vs 60-90) so a
    // continuously-damaged ship doesn't saturate the pool; mild
    // upward velocity so the wisp drifts as a function of intensity
    // rather than billowing instantly. Reuses Kind::Smoke so the
    // existing drag + rise integration applies.
    std::uniform_real_distribution<float> spDist (10.0f, 30.0f);
    std::uniform_real_distribution<float> angDist(-0.8f, 0.8f);  // narrow cone around vertical
    std::uniform_int_distribution<int>    ttlDist(30, 50);
    const float angle = angDist(rng_);
    const float speed = spDist(rng_);
    Particle p{};
    p.x = x;  p.y = y;
    p.vx = std::sin(angle) * speed;
    p.vy = std::cos(angle) * speed + 15.0f;  // mild upward bias
    p.rgb = 0x00606468u;  // dark-gray (same family as explosion smoke)
    const int ttl = ttlDist(rng_);
    p.ttlTicks = static_cast<std::uint16_t>(ttl);
    p.maxTtl   = static_cast<std::uint16_t>(ttl);
    p.pxSize   = 5.5f;
    p.kind     = Kind::Smoke;
    spawn(p);
}

std::uint32_t ParticleSystem::thrustColorForAge(float frac) noexcept {
    // M7.3 §5.1 — lerp per-channel from kThrustColorCool (frac=0) to
    // kThrustColorHot (frac=1). Layout is 0xAABBGGRR (see unpackRGBA
    // in VulkanRenderer.cpp). Alpha bits are left at 0 — caller ORs
    // in the lifetime-decayed alpha.
    const float t = std::clamp(frac, 0.0f, 1.0f);
    auto channel = [&](unsigned shift) {
        const auto cool = static_cast<float>((kThrustColorCool >> shift) & 0xFFu);
        const auto hot  = static_cast<float>((kThrustColorHot  >> shift) & 0xFFu);
        const float v = cool + (hot - cool) * t;
        return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 255.0f));
    };
    const std::uint32_t r = channel(0);
    const std::uint32_t g = channel(8);
    const std::uint32_t b = channel(16);
    return r | (g << 8) | (b << 16);
}

std::uint32_t ParticleSystem::damageSmokeInterval(float hpFrac) noexcept {
    // M7.3 §5.2 — monotonically decreasing interval as hpFrac drops.
    // Above threshold (default 0.4): 0 (no emission). At threshold:
    // large interval (~30 ticks ≈ 0.5 s/puff). At hpFrac=0: minimum
    // interval (3 ticks ≈ 20 puffs/sec). The linear mapping is:
    //   damage = clamp((threshold - hpFrac) / threshold, 0, 1)
    //   interval = round( lerp(kMaxInterval, kMinInterval, damage) )
    // Picked so the visible cadence telegraphs "this ship is on its
    // way out" well before death without overflowing the 256-particle
    // pool when several ships are simultaneously damaged.
    if (hpFrac >= kDamageSmokeFracThreshold) return 0;
    constexpr float kMaxInterval = 30.0f;  // ticks/puff at hpFrac=threshold
    constexpr float kMinInterval = 3.0f;   // ticks/puff at hpFrac=0
    const float clamped = std::clamp(hpFrac, 0.0f, kDamageSmokeFracThreshold);
    const float damage  = (kDamageSmokeFracThreshold - clamped) / kDamageSmokeFracThreshold;
    const float interval = kMaxInterval + (kMinInterval - kMaxInterval) * damage;
    return static_cast<std::uint32_t>(std::max(1.0f, std::floor(interval + 0.5f)));
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
