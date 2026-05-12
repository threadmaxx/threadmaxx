#include "BoidsSystem.hpp"

#include "BoidsConfig.hpp"

#include <threadmaxx/World.hpp>

#include <cmath>
#include <cstdint>

namespace {

inline float length2D(const threadmaxx::Vec3& v) {
    return std::sqrt(v.x * v.x + v.z * v.z);
}

inline threadmaxx::Vec3 limit2D(threadmaxx::Vec3 v, float maxLen) {
    const float len = length2D(v);
    if (len > maxLen && len > 0.0f) {
        const float s = maxLen / len;
        v.x *= s;
        v.z *= s;
    }
    return v;
}

// Returns a unit vector along `v` (XZ plane) scaled to `target`, or zero if v
// has no length. Used to turn an averaged direction into a "desired velocity"
// before computing the steering delta.
inline threadmaxx::Vec3 setMagnitude2D(threadmaxx::Vec3 v, float target) {
    const float len = length2D(v);
    if (len <= 0.0f) return {0.0f, 0.0f, 0.0f};
    const float s = target / len;
    return {v.x * s, 0.0f, v.z * s};
}

} // namespace

void BoidsSystem::update(threadmaxx::SystemContext& ctx) {
    const auto entities   = ctx.world().entities();
    const auto transforms = ctx.world().transforms();
    const auto velocities = ctx.world().velocities();
    const auto dt         = static_cast<float>(ctx.dt());

    const auto N = static_cast<std::uint32_t>(entities.size());
    if (N == 0) return;

    constexpr float perceptionR2 = boids::kPerceptionRadius * boids::kPerceptionRadius;
    constexpr float separationR2 = boids::kSeparationRadius * boids::kSeparationRadius;

    ctx.parallelFor(N, /*grain*/ 32,
        [=](threadmaxx::Range r, threadmaxx::CommandBuffer& cb) {
            for (auto i = r.begin; i < r.end; ++i) {
                const threadmaxx::Vec3 pos = transforms[i].position;
                const threadmaxx::Vec3 vel = velocities[i].linear;

                threadmaxx::Vec3 alignSum{};
                threadmaxx::Vec3 cohesionSum{};
                threadmaxx::Vec3 separationSum{};
                std::uint32_t neighborCount = 0;
                std::uint32_t separationCount = 0;

                for (std::uint32_t j = 0; j < N; ++j) {
                    if (j == i) continue;
                    const threadmaxx::Vec3 d{transforms[j].position.x - pos.x,
                                             0.0f,
                                             transforms[j].position.z - pos.z};
                    const float d2 = d.x * d.x + d.z * d.z;
                    if (d2 > perceptionR2) continue;

                    alignSum.x    += velocities[j].linear.x;
                    alignSum.z    += velocities[j].linear.z;
                    cohesionSum.x += transforms[j].position.x;
                    cohesionSum.z += transforms[j].position.z;
                    ++neighborCount;

                    if (d2 < separationR2 && d2 > 0.0f) {
                        // Repel along -d, weighted by 1/dist so close neighbors push harder.
                        const float invDist = 1.0f / std::sqrt(d2);
                        separationSum.x -= d.x * invDist;
                        separationSum.z -= d.z * invDist;
                        ++separationCount;
                    }
                }

                threadmaxx::Vec3 steer{};
                if (neighborCount > 0) {
                    const float invN = 1.0f / static_cast<float>(neighborCount);
                    const threadmaxx::Vec3 alignAvg{alignSum.x * invN, 0.0f, alignSum.z * invN};
                    const threadmaxx::Vec3 cohAvg{cohesionSum.x * invN, 0.0f, cohesionSum.z * invN};
                    const threadmaxx::Vec3 toCenter{cohAvg.x - pos.x, 0.0f, cohAvg.z - pos.z};

                    const threadmaxx::Vec3 desiredAlign = setMagnitude2D(alignAvg,  boids::kMaxSpeed);
                    const threadmaxx::Vec3 desiredCoh   = setMagnitude2D(toCenter,  boids::kMaxSpeed);

                    threadmaxx::Vec3 sAlign{desiredAlign.x - vel.x, 0.0f, desiredAlign.z - vel.z};
                    threadmaxx::Vec3 sCoh  {desiredCoh.x   - vel.x, 0.0f, desiredCoh.z   - vel.z};
                    sAlign = limit2D(sAlign, boids::kMaxForce);
                    sCoh   = limit2D(sCoh,   boids::kMaxForce);

                    steer.x += sAlign.x * boids::kAlignWeight + sCoh.x * boids::kCohesionWeight;
                    steer.z += sAlign.z * boids::kAlignWeight + sCoh.z * boids::kCohesionWeight;
                }
                if (separationCount > 0) {
                    const threadmaxx::Vec3 desiredSep = setMagnitude2D(separationSum, boids::kMaxSpeed);
                    threadmaxx::Vec3 sSep{desiredSep.x - vel.x, 0.0f, desiredSep.z - vel.z};
                    sSep = limit2D(sSep, boids::kMaxForce);
                    steer.x += sSep.x * boids::kSeparationWeight;
                    steer.z += sSep.z * boids::kSeparationWeight;
                }

                threadmaxx::Velocity nextV = velocities[i];
                nextV.linear.x = vel.x + steer.x * dt;
                nextV.linear.z = vel.z + steer.z * dt;
                nextV.linear   = limit2D(nextV.linear, boids::kMaxSpeed);

                cb.setVelocity(entities[i], nextV);
            }
        });
}
