#include "MyGame.hpp"

#include <threadmaxx/Engine.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {
std::atomic<threadmaxx::Engine*> g_engine{nullptr};

void onSignal(int /*sig*/) {
    if (auto* e = g_engine.load()) e->requestQuit();
}
} // namespace

int main(int argc, char** argv) {
    threadmaxx::Config cfg;
    cfg.workerCount = 0;            // auto
    cfg.fixedStepSeconds = 1.0 / 60.0;
    cfg.deterministic = false;
    cfg.sleepToPace = true;
    cfg.initialEntityCapacity = 4096;

    // Allow a "tick budget" arg so this example exits on its own in CI.
    std::uint64_t maxTicks = 0;
    if (argc > 1) {
        maxTicks = std::strtoull(argv[1], nullptr, 10);
    }

    threadmaxx::Engine engine(cfg);
    MyGame game;
    if (!engine.initialize(game)) {
        std::fprintf(stderr, "engine.initialize() failed\n");
        return 1;
    }

    g_engine.store(&engine);
    std::signal(SIGINT, onSignal);

    if (maxTicks == 0) {
        // Interactive mode: run until Ctrl-C.
        std::printf("[main] running — Ctrl-C to quit\n");
        engine.run();
    } else {
        // Bounded mode: drive step() ourselves so the example finishes.
        std::printf("[main] running %llu ticks\n",
                    static_cast<unsigned long long>(maxTicks));
        for (std::uint64_t i = 0; i < maxTicks && !engine.quitRequested(); ++i) {
            engine.step();
        }
    }

    engine.shutdown();
    g_engine.store(nullptr);
    return 0;
}
