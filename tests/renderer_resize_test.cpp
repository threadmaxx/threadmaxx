// §3.6.5 batch 15a — `IRenderer::onResize` + `Engine::notifyResize`.
//
// Game code forwards platform resize events into the engine via
// `Engine::notifyResize(w, h)`. The engine forwards to whichever
// renderer is installed; no renderer → no-op. The test covers:
//
//   (1) Single call lands on the renderer with the right
//       (width, height).
//   (2) Multiple resizes are delivered in order.
//   (3) Calling with no renderer installed is a no-op (no crash).
//   (4) Replacing the renderer routes future resizes to the new sink.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace {

using namespace threadmaxx;

class ResizableRenderer : public IRenderer {
public:
    void submitFrame(const RenderFrame&) override {}
    void onResize(std::uint32_t w, std::uint32_t h) override {
        events.emplace_back(w, h);
    }
    std::vector<std::pair<std::uint32_t, std::uint32_t>> events;
};

} // namespace

int main() {
    Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;

    // ---- (1) Single resize delivered to the installed renderer.
    {
        Engine engine(cfg);
        ResizableRenderer r;
        engine.setRenderer(&r);
        struct G : IGame {
            void onSetup(Engine&, World&, CommandBuffer&) override {}
        } g;
        CHECK(engine.initialize(g));
        engine.notifyResize(800, 600);
        CHECK_EQ(r.events.size(), std::size_t{1});
        CHECK_EQ(r.events[0].first,  std::uint32_t{800});
        CHECK_EQ(r.events[0].second, std::uint32_t{600});
        engine.shutdown();
    }

    // ---- (2) Multiple resizes in order.
    {
        Engine engine(cfg);
        ResizableRenderer r;
        engine.setRenderer(&r);
        struct G : IGame {
            void onSetup(Engine&, World&, CommandBuffer&) override {}
        } g;
        CHECK(engine.initialize(g));
        engine.notifyResize(1024, 768);
        engine.notifyResize(1920, 1080);
        engine.notifyResize(2560, 1440);
        CHECK_EQ(r.events.size(), std::size_t{3});
        CHECK_EQ(r.events[2].first,  std::uint32_t{2560});
        CHECK_EQ(r.events[2].second, std::uint32_t{1440});
        engine.shutdown();
    }

    // ---- (3) No renderer installed → no-op (no crash).
    {
        Engine engine(cfg);
        struct G : IGame {
            void onSetup(Engine&, World&, CommandBuffer&) override {}
        } g;
        CHECK(engine.initialize(g));
        engine.notifyResize(100, 100);  // must not crash
        engine.shutdown();
    }

    // ---- (4) Replacing the renderer routes future resizes.
    {
        Engine engine(cfg);
        ResizableRenderer first;
        engine.setRenderer(&first);
        struct G : IGame {
            void onSetup(Engine&, World&, CommandBuffer&) override {}
        } g;
        CHECK(engine.initialize(g));
        engine.notifyResize(640, 480);
        ResizableRenderer second;
        engine.setRenderer(&second);
        engine.notifyResize(800, 600);
        CHECK_EQ(first.events.size(),  std::size_t{1});
        CHECK_EQ(second.events.size(), std::size_t{1});
        CHECK_EQ(second.events[0].first,  std::uint32_t{800});
        engine.shutdown();
    }

    // ---- (5) Engine::workerCount mirrors JobSystem.
    {
        Config c2; c2.sleepToPace = false; c2.workerCount = 3;
        Engine engine(c2);
        struct G : IGame {
            void onSetup(Engine&, World&, CommandBuffer&) override {}
        } g;
        CHECK(engine.initialize(g));
        CHECK_EQ(engine.workerCount(), std::uint32_t{3});
        CHECK_EQ(engine.workerCount(),
                 engine.jobSystemStats().workerCount);
        engine.shutdown();
    }

    EXIT_WITH_RESULT();
}
