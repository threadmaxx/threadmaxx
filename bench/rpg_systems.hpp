// §3.10.4 batch 26/27 — Shared system definitions for the RPG-shaped
// stress bench harness.
//
// `MovementSystem`, `BrainSystem`, `RenderPrepSystem` mirror the
// rpg_demo `--stress` system mix at the engine level (no Vulkan, no
// user components). They live in this header so both
// `rpg_stress_bench` (B26 production bench) and `rpg_stress_probe`
// (B27 diagnostic) can register the same workload without duplicating
// the implementations.
//
// All three systems are intentionally simple — the cost we measure is
// the engine framework around them (chunk iteration, command buffer
// recording, commit-phase hashing, wave dispatch), not the per-entity
// body work.

#pragma once

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>
#include <cstdint>
#include <span>

namespace threadmaxx_bench {

// Parallel chunk-walking integrator. Reads (Transform, Velocity);
// writes Transform via the command buffer. Mirrors MovementSystem in
// `examples/rpg_demo`.
class MovementSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "movement"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform}
             | threadmaxx::ComponentSet{threadmaxx::Component::Velocity};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform};
    }
    void update(threadmaxx::SystemContext& ctx) override {
        const float dt = static_cast<float>(ctx.dt());
        threadmaxx::forEachChunk<threadmaxx::Transform, threadmaxx::Velocity>(ctx,
            [dt](std::span<const threadmaxx::EntityHandle> es,
                 std::span<const threadmaxx::Transform> trs,
                 std::span<const threadmaxx::Velocity> vels,
                 threadmaxx::CommandBuffer& cb) {
                for (std::size_t i = 0; i < es.size(); ++i) {
                    threadmaxx::Transform t = trs[i];
                    t.position.x += vels[i].linear.x * dt;
                    t.position.y += vels[i].linear.y * dt;
                    t.position.z += vels[i].linear.z * dt;
                    cb.setTransform(es[i], t);
                }
            });
    }
};

// `ctx.single` serial body that walks every Faction-bearing chunk
// row-by-row. Mirrors NPCBrainSystem's serial RNG-bound path. Touches
// data but writes nothing — the cost is the iteration shape, not the
// commit churn (commit cost is already exercised by `commit_path_bench`).
class BrainSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "brain"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform}
             | threadmaxx::ComponentSet{threadmaxx::Component::Faction};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    void update(threadmaxx::SystemContext& ctx) override {
        ctx.single([this, &ctx](threadmaxx::Range, threadmaxx::CommandBuffer&) {
            std::uint64_t sum = 0;
            const auto chunks = ctx.worldView().chunks();
            for (const auto* c : chunks) {
                if (c == nullptr) continue;
                if (!c->mask.has(threadmaxx::Component::Faction)) continue;
                if (c->mask.has(threadmaxx::Component::DisabledTag)) continue;
                const auto n = c->entities.size();
                for (std::size_t i = 0; i < n; ++i) {
                    sum += c->factions[i].id;
                    sum += static_cast<std::uint64_t>(
                        c->transforms[i].position.x * 100.0f);
                }
            }
            sink_.store(sum, std::memory_order_relaxed);
        });
    }
private:
    std::atomic<std::uint64_t> sink_{0};
};

// Parallel chunk-walking accumulator. Mirrors a render-prep pass: read
// every Transform-bearing chunk, accumulate per-chunk results without
// writing anything. The bench measures iteration framework cost, not
// the per-entity body (the body is intentionally cheap).
class RenderPrepSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "renderprep"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    void update(threadmaxx::SystemContext& ctx) override {
        threadmaxx::forEachChunk<threadmaxx::Transform>(ctx,
            [this](std::span<const threadmaxx::EntityHandle> es,
                   std::span<const threadmaxx::Transform> trs,
                   threadmaxx::CommandBuffer&) {
                float acc = 0.0f;
                for (std::size_t i = 0; i < es.size(); ++i) {
                    acc += trs[i].position.x + trs[i].position.z;
                }
                sink_.fetch_add(static_cast<std::uint64_t>(acc),
                                std::memory_order_relaxed);
            });
    }
private:
    std::atomic<std::uint64_t> sink_{0};
};

} // namespace threadmaxx_bench
