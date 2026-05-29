// Public `Engine::submitInterpolatedFrame(float)` — paused-host re-submit
// hook. Pins the contract used by tou2d's pause/menu flow: when the host
// drives `step()` manually and the engine is paused, `step()` is a no-op
// and never calls `submitFrame`, so the swapchain freezes on the last
// pre-pause image. Calling `submitInterpolatedFrame(alpha)` re-presents
// the existing front frame with the supplied alpha, picking up any
// renderer-side texture updates (e.g. UI overlay bitmap edits) that
// landed since the last submit.
//
// What this test pins:
//   1. Initial `initialize()` submit lands at alpha=0 (sanity).
//   2. `step()` while paused does NOT call `submitFrame` (existing
//      contract — pinned elsewhere by `render_integration_test.cpp`;
//      asserted here as a precondition so a regression there is loud).
//   3. `submitInterpolatedFrame(a)` while paused DOES call `submitFrame`
//      and the renderer sees `frame.alpha == a`.
//   4. The submitted frame's `tick` is unchanged across paused re-submits
//      (world state is NOT rebuilt — only `alpha` is overwritten).
//   5. Calls are idempotent / repeatable — N calls produce N submits.
//   6. Unpausing and stepping returns to the normal alpha=0 path.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <vector>

namespace {

class RecordingRenderer : public threadmaxx::IRenderer {
public:
    struct Captured {
        float alpha;
        std::uint64_t tick;
    };
    std::vector<Captured> frames;
    bool initialize() override { return true; }
    void shutdown() override {}
    void submitFrame(const threadmaxx::RenderFrame& frame) override {
        frames.push_back(Captured{frame.alpha, frame.tick});
    }
};

class EmptyGame : public threadmaxx::IGame {
public:
    threadmaxx::IRenderer* renderer = nullptr;
    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {
        engine.setRenderer(renderer);
    }
};

} // namespace

int main() {
    threadmaxx::Config cfg;
    cfg.fixedStepSeconds = 1.0 / 60.0;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;

    RecordingRenderer renderer;
    threadmaxx::Engine engine(cfg);
    EmptyGame game;
    game.renderer = &renderer;
    CHECK(engine.initialize(game));

    // (1) initialize() publishes the first frame with alpha=0, tick=0.
    CHECK_EQ(renderer.frames.size(), std::size_t{1});
    CHECK(renderer.frames[0].alpha == 0.0f);
    CHECK_EQ(renderer.frames[0].tick, std::uint64_t{0});

    // Pause and (2) confirm step() is a no-op (no new submitFrame call).
    engine.setPaused(true);
    engine.step();
    CHECK_EQ(renderer.frames.size(), std::size_t{1});
    CHECK_EQ(engine.tick(), std::uint64_t{0});

    // (3) submitInterpolatedFrame while paused → fresh submitFrame with
    // the supplied alpha.
    engine.submitInterpolatedFrame(0.25f);
    CHECK_EQ(renderer.frames.size(), std::size_t{2});
    CHECK(renderer.frames[1].alpha == 0.25f);
    // (4) tick is unchanged across the paused re-submit.
    CHECK_EQ(renderer.frames[1].tick, std::uint64_t{0});
    CHECK_EQ(engine.tick(), std::uint64_t{0});

    // (5) N calls → N submits, alpha overwritten each call.
    engine.submitInterpolatedFrame(0.75f);
    engine.submitInterpolatedFrame(0.5f);
    CHECK_EQ(renderer.frames.size(), std::size_t{4});
    CHECK(renderer.frames[2].alpha == 0.75f);
    CHECK(renderer.frames[3].alpha == 0.5f);
    CHECK_EQ(renderer.frames[2].tick, std::uint64_t{0});
    CHECK_EQ(renderer.frames[3].tick, std::uint64_t{0});

    // (6) Unpause + step → normal post-tick submit, alpha=0, tick=1.
    engine.setPaused(false);
    engine.step();
    CHECK_EQ(renderer.frames.size(), std::size_t{5});
    CHECK(renderer.frames[4].alpha == 0.0f);
    CHECK_EQ(renderer.frames[4].tick, std::uint64_t{1});

    // Alpha=0 also works (used by tou2d's paused main loop literally —
    // sim state didn't advance, so 0 is the semantically correct value).
    engine.setPaused(true);
    engine.submitInterpolatedFrame(0.0f);
    CHECK_EQ(renderer.frames.size(), std::size_t{6});
    CHECK(renderer.frames[5].alpha == 0.0f);
    CHECK_EQ(renderer.frames[5].tick, std::uint64_t{1});

    engine.shutdown();
    EXIT_WITH_RESULT();
}
