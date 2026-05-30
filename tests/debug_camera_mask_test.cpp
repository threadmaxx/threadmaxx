// debug_camera_mask_test — pins the M7.2 cameraMask contract on
// `DebugLine` / `DebugPoint`.
//
// Contract:
//   (1) Default-constructed `DebugLine` / `DebugPoint` have
//       `cameraMask == 0xFFFFFFFFu` (the renderer-neutral "visible
//       from every camera" sentinel).
//   (2) A producer that sets `cameraMask = (1u << k)` round-trips
//       the value bit-for-bit through `RenderFrameBuilder` and the
//       resulting `RenderFrame` span.
//   (3) Multiple primitives with different masks coexist; ordering
//       (the builder is FIFO) is preserved.
//   (4) The masks survive across a `step()` boundary (the engine
//       merges every system's RenderFrameBuilder into the back
//       frame; the field must not get truncated or zeroed).

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace {

using namespace threadmaxx;

class MaskedDebugEmitter : public ISystem {
public:
    const char* name() const noexcept override { return "masked-debug-emitter"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }
    void update(SystemContext&) override {}
    void buildRenderFrame(RenderFrameBuilder& b) override {
        // Three lines: cameras-0-only, cameras-1-only, all-cameras.
        DebugLine la{}; la.a = {0,0,0}; la.b = {1,0,0};
        la.cameraMask = (1u << 0);
        b.addDebugLine(la);

        DebugLine lb{}; lb.a = {0,0,0}; lb.b = {0,1,0};
        lb.cameraMask = (1u << 1);
        b.addDebugLine(lb);

        DebugLine lc{}; lc.a = {0,0,0}; lc.b = {0,0,1};
        // lc.cameraMask intentionally left as default.
        b.addDebugLine(lc);

        // Two points: slot-3-only, all-cameras.
        DebugPoint pa{}; pa.position = {1,1,1};
        pa.cameraMask = (1u << 3);
        b.addDebugPoint(pa);

        DebugPoint pb{}; pb.position = {2,2,2};
        // pb.cameraMask intentionally left as default.
        b.addDebugPoint(pb);
    }
};

class CapturingRenderer : public IRenderer {
public:
    void submitFrame(const RenderFrame& frame) override {
        lineMasks.clear();
        pointMasks.clear();
        for (const auto& l : frame.debugLines)  lineMasks.push_back(l.cameraMask);
        for (const auto& p : frame.debugPoints) pointMasks.push_back(p.cameraMask);
        ++frames;
    }
    std::vector<std::uint32_t> lineMasks;
    std::vector<std::uint32_t> pointMasks;
    int frames = 0;
};

class Game : public IGame {
public:
    void onSetup(Engine& engine, World&, CommandBuffer&) override {
        engine.registerSystem(std::make_unique<MaskedDebugEmitter>());
    }
};

} // namespace

int main() {
    // (1) Default-construction sentinels.
    {
        DebugLine l{};
        CHECK_EQ(l.cameraMask, 0xFFFFFFFFu);
        DebugPoint p{};
        CHECK_EQ(p.cameraMask, 0xFFFFFFFFu);
    }

    // (2,3,4) End-to-end round-trip through the engine.
    Engine engine(Config{});
    Game game;
    CapturingRenderer renderer;
    engine.setRenderer(&renderer);
    CHECK(engine.initialize(game));

    // `initialize` publishes an initial frame; `step` publishes the
    // post-tick frame. Either is acceptable here — we only care that
    // the masks survive the publish round-trip.
    engine.step();
    CHECK(renderer.frames >= 1);
    CHECK_EQ(renderer.lineMasks.size(),  static_cast<std::size_t>(3));
    CHECK_EQ(renderer.pointMasks.size(), static_cast<std::size_t>(2));

    // Ordering is FIFO from the builder.
    CHECK_EQ(renderer.lineMasks[0], (1u << 0));
    CHECK_EQ(renderer.lineMasks[1], (1u << 1));
    CHECK_EQ(renderer.lineMasks[2], 0xFFFFFFFFu);

    CHECK_EQ(renderer.pointMasks[0], (1u << 3));
    CHECK_EQ(renderer.pointMasks[1], 0xFFFFFFFFu);

    // Step again — the builder resets per tick, but the emitter
    // re-emits with the same masks. Confirms (4): no truncation
    // across the reset/merge boundary.
    engine.step();
    CHECK_EQ(renderer.lineMasks.size(),  static_cast<std::size_t>(3));
    CHECK_EQ(renderer.lineMasks[0], (1u << 0));
    CHECK_EQ(renderer.lineMasks[1], (1u << 1));
    CHECK_EQ(renderer.lineMasks[2], 0xFFFFFFFFu);
    CHECK_EQ(renderer.pointMasks[0], (1u << 3));
    CHECK_EQ(renderer.pointMasks[1], 0xFFFFFFFFu);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
