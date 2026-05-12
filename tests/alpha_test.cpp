// Render interpolation alpha: step() submits frames with alpha=0; run()
// also submits between-step frames with 0 <= alpha < 1.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <chrono>
#include <thread>
#include <vector>

namespace {

// Records every alpha the engine submits to it. Lives on the sim thread —
// submitFrame is synchronous — so no locking needed for single-reader.
class RecordingRenderer : public threadmaxx::IRenderer {
public:
    std::vector<float> alphas;
    bool initialize() override { return true; }
    void shutdown() override {}
    void submitFrame(const threadmaxx::RenderFrame& frame) override {
        alphas.push_back(frame.alpha);
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
    cfg.fixedStepSeconds = 1.0 / 240.0;  // 240 Hz so we tick fast
    cfg.sleepToPace = true;              // realistic timing
    cfg.workerCount = 2;

    RecordingRenderer renderer;
    threadmaxx::Engine engine(cfg);
    EmptyGame game;
    game.renderer = &renderer;
    CHECK(engine.initialize(game));

    // Initial submit (from initialize()) — alpha must be 0.
    CHECK_EQ(renderer.alphas.size(), std::size_t{1});
    CHECK(renderer.alphas[0] == 0.0f);

    // Drive run() in a side thread for a short bit.
    std::thread t([&engine] { engine.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    engine.requestQuit();
    t.join();

    // We should have plenty of submissions by now.
    CHECK(renderer.alphas.size() > 5);

    // Some frames carry alpha == 0 (post-step submits).
    int zeroCount = 0;
    int nonzeroCount = 0;
    bool sawValidRange = true;
    for (float a : renderer.alphas) {
        if (a == 0.0f) ++zeroCount;
        else ++nonzeroCount;
        if (!(a >= 0.0f && a < 1.0f)) sawValidRange = false;
    }
    CHECK(zeroCount >= 1);
    // Interp submits only happen when stepsThisIter > 0 in run(), which is
    // every iteration at 240 Hz — so we expect non-zero alphas too.
    CHECK(nonzeroCount >= 1);
    CHECK(sawValidRange);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
