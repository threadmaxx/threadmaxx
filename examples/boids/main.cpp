#include "BoidsGame.hpp"

#include <threadmaxx/Engine.hpp>

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    threadmaxx::Config cfg;
    cfg.workerCount = 0;
    cfg.fixedStepSeconds = 1.0 / 60.0;
    cfg.sleepToPace = true;
    cfg.initialEntityCapacity = 1024;

    std::uint64_t maxTicks = 0;
    if (argc > 1) maxTicks = std::strtoull(argv[1], nullptr, 10);

    threadmaxx::Engine engine(cfg);
    BoidsGame game(&engine);
    if (!engine.initialize(game)) {
        std::fprintf(stderr, "engine.initialize() failed\n");
        return 1;
    }

    if (maxTicks == 0) {
        std::printf("[main] running — close window or press Esc to quit\n");
        engine.run();
    } else {
        std::printf("[main] running %llu ticks\n",
                    static_cast<unsigned long long>(maxTicks));
        for (std::uint64_t i = 0; i < maxTicks && !engine.quitRequested(); ++i) {
            engine.step();
        }
    }

    engine.shutdown();
    return 0;
}
