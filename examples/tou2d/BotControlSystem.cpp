#include "BotControlSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

#include <cmath>

namespace tou2d {

namespace {

constexpr float kFireRange         = 220.0f;   // open fire inside this distance
constexpr float kThrustHoldDist    = 60.0f;    // closer than this → coast (don't ram)
constexpr float kFacingThrust      = 0.78f;    // ~45° in radians, |angDelta| < this → thrust
constexpr float kFacingFire        = 0.17f;    // ~10° → fire
constexpr float kAngEpsilon        = 0.04f;    // ±2.3° dead-zone — stops jitter
constexpr float kSpecialFireChance = 0.10f;    // ~10% of in-arc ticks

inline float orientationAngleZ(const threadmaxx::Quat& q) noexcept {
    return std::atan2(2.0f * (q.w * q.z + q.x * q.y),
                      1.0f - 2.0f * (q.y * q.y + q.z * q.z));
}

/// Shortest signed angular difference target-current, wrapped into (-π, π].
inline float wrapPi(float a) noexcept {
    constexpr float twoPi = 6.28318530718f;
    constexpr float pi    = 3.14159265359f;
    a = std::fmod(a + pi, twoPi);
    if (a < 0.0f) a += twoPi;
    return a - pi;
}

/// xorshift32 — single-tick PRNG used to dither bot decisions.
inline std::uint32_t xorshift32(std::uint32_t& s) noexcept {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

struct ShipPos {
    threadmaxx::EntityHandle handle{};
    float                    x = 0, y = 0;
};

} // namespace

BotControlSystem::BotControlSystem(UserComponentIds ids) noexcept : ids_(ids) {}

void BotControlSystem::preStep(threadmaxx::SystemContext& ctx) {
    const auto idsPi = ids_.playerInput;
    const auto idsLp = ids_.localPlayer;
    if (!idsPi.valid() || !idsLp.valid()) return;

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();

        // ---- Pass 1: gather positions of every live ship (any slot) -------
        // The bot just needs targets; "live" = LocalPlayer-tagged and NOT
        // DisabledTag. Cheap pre-scan because ≤4 ships ever exist.
        std::array<ShipPos, 16> live;
        std::size_t             liveCount = 0;

        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsLp.componentBit()))               continue;
            if (!chunk.mask.has(threadmaxx::Component::Transform))   continue;
            if (chunk.mask.has(threadmaxx::Component::DisabledTag))  continue;

            const auto& transforms = chunk.transforms;
            const auto  entities   = chunk.entities;
            for (std::size_t row = 0, n = entities.size(); row < n; ++row) {
                if (liveCount >= live.size()) break;
                live[liveCount].handle = entities[row];
                live[liveCount].x      = transforms[row].position.x;
                live[liveCount].y      = transforms[row].position.y;
                ++liveCount;
            }
        }

        // ---- Pass 2: drive each bot ship ----------------------------------
        ++rngState_;  // bump per-tick so all-zero-state bots don't co-fire
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsLp.componentBit()))               continue;
            if (!chunk.mask.has(idsPi.componentBit()))               continue;
            if (!chunk.mask.has(threadmaxx::Component::Transform))   continue;
            if (chunk.mask.has(threadmaxx::Component::DisabledTag))  continue;

            const auto lpSpan      = threadmaxx::user::chunkSpan<LocalPlayer>(chunk, idsLp);
            const auto& transforms = chunk.transforms;
            const auto  entities   = chunk.entities;
            for (std::size_t row = 0, n = entities.size(); row < n; ++row) {
                if (lpSpan[row].isBot == 0) continue;  // human — InputSystem won this slot

                const auto& selfT = transforms[row];

                // Nearest other live ship.
                float            bestD2 = 1e30f;
                const ShipPos*   tgt    = nullptr;
                for (std::size_t i = 0; i < liveCount; ++i) {
                    if (live[i].handle.index == entities[row].index) continue;
                    const float dx = live[i].x - selfT.position.x;
                    const float dy = live[i].y - selfT.position.y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 < bestD2) {
                        bestD2 = d2;
                        tgt    = &live[i];
                    }
                }

                PlayerInput in{};  // all zero — drift if no target

                if (tgt) {
                    const float dx        = tgt->x - selfT.position.x;
                    const float dy        = tgt->y - selfT.position.y;
                    const float range     = std::sqrt(dx * dx + dy * dy);

                    // Atan2 here returns the world-space angle to target;
                    // our Z-rotation forward maps to (-sin θ, cos θ), so
                    // the target angle is atan2(-dx, dy).
                    const float tgtAngle  = std::atan2(-dx, dy);
                    const float curAngle  = orientationAngleZ(selfT.orientation);
                    const float angDelta  = wrapPi(tgtAngle - curAngle);

                    if (angDelta >  kAngEpsilon) in.turnLeft  = 1;
                    if (angDelta < -kAngEpsilon) in.turnRight = 1;

                    if (std::fabs(angDelta) < kFacingThrust && range > kThrustHoldDist) {
                        in.thrust = 1;
                    }

                    if (std::fabs(angDelta) < kFacingFire && range < kFireRange) {
                        in.fireBasic = 1;
                        // Roll for spread shot occasionally.
                        const float r = static_cast<float>(xorshift32(rngState_) >> 8) /
                                        static_cast<float>(1u << 24);
                        if (r < kSpecialFireChance) {
                            in.fireSpecial = 1;
                        }
                    }
                }

                threadmaxx::addUserComponent(cb, idsPi, entities[row], in);
            }
        }
    });
}

} // namespace tou2d
