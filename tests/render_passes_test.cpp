// §3.2 batch 8: RenderFrame's per-pass bins reflect the doc's enum
// ordering; DebugLine/DebugPoint/DebugText round-trip through the
// frame; flat instances array stays auto-populated alongside the
// hierarchical fields.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace {

class AllPassesEmitter : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "all_passes"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
    void update(threadmaxx::SystemContext&) override {}

    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override {
        threadmaxx::DrawItem op; op.meshId = 1;
        b.addDrawItem(threadmaxx::RenderPass::Opaque, op);
        threadmaxx::DrawItem tr; tr.meshId = 2;
        b.addDrawItem(threadmaxx::RenderPass::Transparent, tr);
        threadmaxx::DrawItem sh; sh.meshId = 3;
        b.addDrawItem(threadmaxx::RenderPass::ShadowCasters, sh);
        threadmaxx::DrawItem ov; ov.meshId = 4;
        b.addDrawItem(threadmaxx::RenderPass::Overlay, ov);

        b.addDebugLine(threadmaxx::DebugLine{{0,0,0},{1,1,1},0x01u});
        b.addDebugPoint(threadmaxx::DebugPoint{{2,2,2},0x02u,3.0f});
        b.addDebugText(threadmaxx::DebugText{{3,3,3}, std::string_view{"hi"}, 0x03u});
    }
};

class CapturingRenderer : public threadmaxx::IRenderer {
public:
    void submitFrame(const threadmaxx::RenderFrame& frame) override {
        for (std::size_t p = 0; p < threadmaxx::kRenderPassCount; ++p) {
            binSizes[p] = frame.drawItems[p].size();
            if (!frame.drawItems[p].empty()) {
                binFirstMeshId[p] = frame.drawItems[p][0].meshId;
            }
        }
        debugLineCount = frame.debugLines.size();
        debugPointCount = frame.debugPoints.size();
        debugTextCount = frame.debugText.size();
        instanceCount = frame.instances.size();
    }
    std::size_t binSizes[threadmaxx::kRenderPassCount] = {};
    std::int32_t binFirstMeshId[threadmaxx::kRenderPassCount] = {};
    std::size_t debugLineCount = 0;
    std::size_t debugPointCount = 0;
    std::size_t debugTextCount = 0;
    std::size_t instanceCount = 0;
};

class Game : public threadmaxx::IGame {
public:
    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        engine.registerSystem(std::make_unique<AllPassesEmitter>());
        // Seed one renderable entity so the legacy flat `instances` is
        // also exercised.
        threadmaxx::Transform t;
        threadmaxx::RenderTag rt; rt.meshId = 999;
        seed.spawn(t, threadmaxx::Velocity{}, rt);
    }
};

} // namespace

int main() {
    threadmaxx::Engine engine(threadmaxx::Config{});
    Game game;
    CapturingRenderer renderer;
    engine.setRenderer(&renderer);
    CHECK(engine.initialize(game));

    engine.step();

    // One item per pass, in the documented enum order.
    CHECK_EQ(renderer.binSizes[threadmaxx::passIndex(threadmaxx::RenderPass::Opaque)],        std::size_t{1});
    CHECK_EQ(renderer.binSizes[threadmaxx::passIndex(threadmaxx::RenderPass::Transparent)],   std::size_t{1});
    CHECK_EQ(renderer.binSizes[threadmaxx::passIndex(threadmaxx::RenderPass::ShadowCasters)], std::size_t{1});
    CHECK_EQ(renderer.binSizes[threadmaxx::passIndex(threadmaxx::RenderPass::Overlay)],       std::size_t{1});
    CHECK_EQ(renderer.binFirstMeshId[threadmaxx::passIndex(threadmaxx::RenderPass::Opaque)],        1);
    CHECK_EQ(renderer.binFirstMeshId[threadmaxx::passIndex(threadmaxx::RenderPass::Transparent)],   2);
    CHECK_EQ(renderer.binFirstMeshId[threadmaxx::passIndex(threadmaxx::RenderPass::ShadowCasters)], 3);
    CHECK_EQ(renderer.binFirstMeshId[threadmaxx::passIndex(threadmaxx::RenderPass::Overlay)],       4);

    CHECK_EQ(renderer.debugLineCount, std::size_t{1});
    CHECK_EQ(renderer.debugPointCount, std::size_t{1});
    CHECK_EQ(renderer.debugTextCount, std::size_t{1});

    // Legacy flat `instances` still auto-populated.
    CHECK_EQ(renderer.instanceCount, std::size_t{1});

    engine.shutdown();
    EXIT_WITH_RESULT();
}
