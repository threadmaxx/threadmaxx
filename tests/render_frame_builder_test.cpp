// §3.2 batch 8: ISystem::buildRenderFrame hook merges per-system
// builders into the published RenderFrame in registration order.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace {

class CameraEmitterA : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "cameraA"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
    void update(threadmaxx::SystemContext&) override {}
    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override {
        threadmaxx::Camera c;
        c.id = 100;
        b.addCamera(c);
        threadmaxx::Light l;
        l.intensity = 0.5f;
        b.addLight(l);
        threadmaxx::DrawItem item;
        item.entity = threadmaxx::EntityHandle{};
        item.meshId = 11;
        b.addDrawItem(threadmaxx::RenderPass::Opaque, item);
    }
};

class CameraEmitterB : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "cameraB"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
    void update(threadmaxx::SystemContext&) override {}
    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override {
        threadmaxx::Camera c;
        c.id = 200;
        b.addCamera(c);
        threadmaxx::DrawItem item;
        item.meshId = 22;
        b.addDrawItem(threadmaxx::RenderPass::Transparent, item);
        threadmaxx::DebugLine ln{{0,0,0},{1,1,1},0xFFFF0000u};
        b.addDebugLine(ln);
    }
};

class CapturingRenderer : public threadmaxx::IRenderer {
public:
    bool initialize() override { return true; }
    void submitFrame(const threadmaxx::RenderFrame& frame) override {
        cameras.assign(frame.cameras.begin(), frame.cameras.end());
        lights.assign(frame.lights.begin(), frame.lights.end());
        opaqueItems.assign(frame.drawItems[threadmaxx::passIndex(threadmaxx::RenderPass::Opaque)].begin(),
                           frame.drawItems[threadmaxx::passIndex(threadmaxx::RenderPass::Opaque)].end());
        transparentItems.assign(frame.drawItems[threadmaxx::passIndex(threadmaxx::RenderPass::Transparent)].begin(),
                                frame.drawItems[threadmaxx::passIndex(threadmaxx::RenderPass::Transparent)].end());
        debugLines.assign(frame.debugLines.begin(), frame.debugLines.end());
    }
    void shutdown() override {}

    std::vector<threadmaxx::Camera>    cameras;
    std::vector<threadmaxx::Light>     lights;
    std::vector<threadmaxx::DrawItem>  opaqueItems;
    std::vector<threadmaxx::DrawItem>  transparentItems;
    std::vector<threadmaxx::DebugLine> debugLines;
};

class Game : public threadmaxx::IGame {
public:
    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {
        engine.registerSystem(std::make_unique<CameraEmitterA>());
        engine.registerSystem(std::make_unique<CameraEmitterB>());
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

    CHECK_EQ(renderer.cameras.size(), std::size_t{2});
    CHECK_EQ(renderer.cameras[0].id, 100u);
    CHECK_EQ(renderer.cameras[1].id, 200u);
    CHECK_EQ(renderer.lights.size(), std::size_t{1});
    CHECK(renderer.lights[0].intensity == 0.5f);
    CHECK_EQ(renderer.opaqueItems.size(), std::size_t{1});
    CHECK_EQ(renderer.opaqueItems[0].meshId, 11);
    CHECK_EQ(renderer.transparentItems.size(), std::size_t{1});
    CHECK_EQ(renderer.transparentItems[0].meshId, 22);
    CHECK_EQ(renderer.debugLines.size(), std::size_t{1});
    CHECK_EQ(renderer.debugLines[0].colorRGBA, 0xFFFF0000u);

    // Second tick: builders reset, no duplication.
    engine.step();
    CHECK_EQ(renderer.cameras.size(), std::size_t{2});
    CHECK_EQ(renderer.opaqueItems.size(), std::size_t{1});

    engine.shutdown();
    EXIT_WITH_RESULT();
}
