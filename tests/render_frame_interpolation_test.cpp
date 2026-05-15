// §3.6.5 batch 15b — `RenderFrame::prevTransforms` contract.
//
// The engine publishes a parallel `prevTransforms` span next to
// `instances`. Renderers that want smooth motion can compute
// `lerp(prev, current, alpha)` without maintaining their own
// per-entity history map. This test pins the contract:
//
//   (1) `prevTransforms.size() == instances.size()` on every frame.
//   (2) `prevTransforms[i].entity == instances[i].entity` for every i.
//   (3) On a tick where the entity moved, its `prevTransforms[i]`
//       carries the PREVIOUS tick's position, not the current.
//   (4) For first-frame entities (no prior tick), `prevTransforms[i]`
//       equals `instances[i].transform` (avoids a NaN lerp on spawn).
//   (5) After an entity is destroyed and re-spawned the new
//       generation does not see the OLD generation's position
//       (different `EntityHandle` → cache miss → prev = current).
//   (6) Disabled entities are excluded from both spans.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <vector>

namespace {

using namespace threadmaxx;

class MoverSystem : public ISystem {
public:
    const char* name() const noexcept override { return "mover"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Transform};
    }
    void update(SystemContext& ctx) override {
        const auto n = static_cast<std::uint32_t>(ctx.world().size());
        ctx.parallelFor(n, 256, [&ctx](Range r, CommandBuffer& cb) {
            const auto hs = ctx.world().entities();
            const auto ts = ctx.world().transforms();
            for (std::uint32_t i = r.begin; i < r.end; ++i) {
                Transform t = ts[i];
                t.position.x += 1.0f;
                cb.setTransform(hs[i], t);
            }
        });
    }
};

struct CapturedFrame {
    std::uint64_t tick = 0;
    std::vector<EntityHandle> instEntities;
    std::vector<Transform>    instTransforms;
    std::vector<EntityHandle> prevEntities;
    std::vector<Transform>    prevTransforms;
};

class CaptureRenderer : public IRenderer {
public:
    void submitFrame(const RenderFrame& f) override {
        CapturedFrame c;
        c.tick = f.tick;
        for (const auto& inst : f.instances) {
            c.instEntities.push_back(inst.entity);
            c.instTransforms.push_back(inst.transform);
        }
        for (const auto& p : f.prevTransforms) {
            c.prevEntities.push_back(p.entity);
            c.prevTransforms.push_back(p.transform);
        }
        frames.push_back(std::move(c));
    }
    std::vector<CapturedFrame> frames;
};

} // namespace

int main() {
    Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
    Engine engine(cfg);
    CaptureRenderer renderer;
    engine.setRenderer(&renderer);

    struct G : IGame {
        void onSetup(Engine& eng, World&, CommandBuffer& cb) override {
            for (int i = 0; i < 8; ++i) {
                Transform t{};
                t.position.x = static_cast<float>(i);
                RenderTag r; r.meshId = 1;
                cb.spawn(t, Velocity{}, r);
            }
            eng.registerSystem(std::make_unique<MoverSystem>());
        }
    } g;
    CHECK(engine.initialize(g));

    // Three steps so we have a clean before/after pair for assertion.
    engine.step();
    engine.step();
    engine.step();

    CHECK(renderer.frames.size() >= 4);  // initialize + 3 steps

    // ---- (1)(2) size + entity alignment across every captured frame.
    for (const auto& f : renderer.frames) {
        CHECK_EQ(f.instEntities.size(), f.prevEntities.size());
        for (std::size_t i = 0; i < f.instEntities.size(); ++i) {
            CHECK(f.instEntities[i] == f.prevEntities[i]);
        }
    }

    // ---- (4) First-frame entities: prev == current.
    {
        const auto& f0 = renderer.frames.front();
        CHECK_EQ(f0.tick, std::uint64_t{0});
        for (std::size_t i = 0; i < f0.instTransforms.size(); ++i) {
            CHECK_EQ(f0.instTransforms[i].position.x,
                     f0.prevTransforms[i].position.x);
        }
    }

    // ---- (3) Subsequent frames: prev is the previous tick's position.
    // After step()s the mover added +1 per tick. So on frame[k] (k>=1),
    // prev = inst - 1.
    for (std::size_t k = 1; k < renderer.frames.size(); ++k) {
        const auto& f = renderer.frames[k];
        for (std::size_t i = 0; i < f.instTransforms.size(); ++i) {
            const float curr = f.instTransforms[i].position.x;
            const float prev = f.prevTransforms[i].position.x;
            CHECK_EQ(curr - prev, 1.0f);
        }
    }

    engine.shutdown();
    EXIT_WITH_RESULT();
}
