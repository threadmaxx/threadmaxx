#include "ParticleEmitterSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <cmath>
#include <utility>

namespace rpg {

void ParticleEmitterSystem::preStep(threadmaxx::SystemContext& ctx) {
    bursts_.clear();
    const auto& w = ctx.world();

    // Sword hits / NPC swings → bright spark burst at the hit point.
    {
        const float color[4] = {1.0f, 0.85f, 0.35f, 1.0f};
        const auto evs = engine_->events<DamageDealt>().drainTick();
        for (const auto& d : evs) {
            Burst b;
            b.x = d.posX; b.y = d.posY; b.z = d.posZ;
            b.count = kParticlesPerSwordHit;
            b.speed = kParticleSparkSpeed;
            b.lifeSeconds = kParticleSparkLifeSeconds;
            b.color[0] = color[0]; b.color[1] = color[1];
            b.color[2] = color[2]; b.color[3] = color[3];
            bursts_.push_back(b);
        }
    }

    // Deaths → big puff of dark dust at the death position.
    {
        const float color[4] = {0.65f, 0.25f, 0.25f, 1.0f};
        const auto evs = engine_->events<EntityDied>().drainTick();
        for (const auto& d : evs) {
            Burst b;
            b.x = d.posX; b.y = d.posY; b.z = d.posZ;
            b.count = kParticlesPerDeath;
            b.speed = kParticlePuffSpeed;
            b.lifeSeconds = kParticlePuffLifeSeconds;
            b.color[0] = color[0]; b.color[1] = color[1];
            b.color[2] = color[2]; b.color[3] = color[3];
            bursts_.push_back(b);
        }
    }

    // Pickups → soft dust ring at the pickup's last known position. We
    // read the (now-DisabledTag'd-but-still-alive) pickup's Transform
    // rather than the player's because the visual lives where the
    // pickup was, not under the player's feet.
    {
        const float color[4] = {1.0f, 0.95f, 0.55f, 1.0f};
        const auto evs = engine_->events<PickupCollected>().drainTick();
        for (const auto& ev : evs) {
            const auto* tr = w.tryGetTransform(ev.pickup);
            if (!tr) continue;
            Burst b;
            b.x = tr->position.x; b.y = tr->position.y; b.z = tr->position.z;
            b.count = kParticlesPerPickup;
            b.speed = kParticleDustSpeed;
            b.lifeSeconds = kParticleDustLifeSeconds;
            b.color[0] = color[0]; b.color[1] = color[1];
            b.color[2] = color[2]; b.color[3] = color[3];
            bursts_.push_back(b);
        }
    }

    // 2026-05-22 audit (round 6) — jump landings → low dust puff at
    // the player's feet. Slightly cooler / dimmer color than the
    // pickup dust so the two reads as visually distinct events.
    {
        const float color[4] = {0.78f, 0.72f, 0.62f, 1.0f};
        const auto evs = engine_->events<JumpLanded>().drainTick();
        for (const auto& ev : evs) {
            Burst b;
            b.x = ev.posX; b.y = ev.posY; b.z = ev.posZ;
            b.count = kParticlesPerLanding;
            b.speed = kParticleLandingSpeed;
            b.lifeSeconds = kParticleLandingLifeSeconds;
            b.color[0] = color[0]; b.color[1] = color[1];
            b.color[2] = color[2]; b.color[3] = color[3];
            bursts_.push_back(b);
        }
    }
}

void ParticleEmitterSystem::update(threadmaxx::SystemContext& ctx) {
    if (bursts_.empty()) return;

    // Pre-reserve every handle on the sim thread under one lock
    // acquisition. The §3.5 batch-2 batch reservation API is built
    // for exactly this — N spawns coming from one job.
    std::uint32_t totalCount = 0;
    for (const auto& b : bursts_) totalCount += b.count;
    if (totalCount == 0) return;

    std::vector<threadmaxx::EntityHandle> handles(totalCount);
    const std::uint32_t got = ctx.reserveHandles(
        totalCount, std::span<threadmaxx::EntityHandle>(handles));
    if (got == 0) return;
    handles.resize(got);

    // Pre-roll per-particle velocity directions on the sim thread so
    // the `single` lambda only does the spawn assembly. RNG is also
    // single-threaded here — the lambda only consumes the prerolled
    // values.
    struct Spawn {
        threadmaxx::EntityHandle handle;
        float posX, posY, posZ;
        float velX, velY, velZ;
        float color[4];
        float life;
    };
    std::vector<Spawn> spawns;
    spawns.reserve(got);

    std::uniform_real_distribution<float> angleDist(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> upDist(0.2f, 0.9f);
    std::uniform_real_distribution<float> jitterDist(-0.15f, 0.15f);

    std::uint32_t handleCursor = 0;
    for (const auto& b : bursts_) {
        for (std::uint32_t i = 0; i < b.count; ++i) {
            if (handleCursor >= got) break;
            const float a = angleDist(rng_);
            const float up = upDist(rng_);
            const float planar = std::sqrt(std::max(0.0f, 1.0f - up * up));
            Spawn s;
            s.handle = handles[handleCursor++];
            s.posX = b.x + jitterDist(rng_);
            s.posY = b.y + jitterDist(rng_);
            s.posZ = b.z + jitterDist(rng_);
            s.velX = std::cos(a) * planar * b.speed;
            s.velY = up * b.speed;
            s.velZ = std::sin(a) * planar * b.speed;
            s.color[0] = b.color[0]; s.color[1] = b.color[1];
            s.color[2] = b.color[2]; s.color[3] = b.color[3];
            s.life = b.lifeSeconds;
            spawns.push_back(s);
        }
        if (handleCursor >= got) break;
    }

    const float simTime = static_cast<float>(ctx.tick()) *
                          static_cast<float>(ctx.dt());
    const auto* ids = ids_;

    ctx.single([spawns = std::move(spawns), ids, simTime]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        for (const auto& s : spawns) {
            threadmaxx::Bundle b{};
            b.transform.position = {s.posX, s.posY, s.posZ};
            b.transform.scale    = {kParticleScale,
                                    kParticleScale,
                                    kParticleScale};
            b.velocity = threadmaxx::Velocity{
                {s.velX, s.velY, s.velZ}, {0, 0, 0}};
            b.initialMask = threadmaxx::ComponentSet{
                threadmaxx::Component::Transform,
                threadmaxx::Component::Velocity,
            };
            cb.spawnBundle(s.handle, b);

            CubeRender cr;
            cr.color[0] = s.color[0]; cr.color[1] = s.color[1];
            cr.color[2] = s.color[2]; cr.color[3] = s.color[3];
            cr.scale = 1.0f;
            threadmaxx::addUserComponent(cb, ids->cubeRender, s.handle, cr);

            Particle p;
            p.spawnTimeSeconds = simTime;
            p.initialLifetime  = s.life;
            p.color[0] = s.color[0]; p.color[1] = s.color[1];
            p.color[2] = s.color[2]; p.color[3] = s.color[3];
            p.fadeSeconds = 0.2f;
            threadmaxx::addUserComponent(cb, ids->particle, s.handle, p);
        }
    });
}

} // namespace rpg
