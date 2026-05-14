// §3.2 batch 7: Engine::preloadUntil pumps loaders synchronously until
// a predicate returns true or a timeout elapses. The simulation does
// not advance; only loader update()s run.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <chrono>

namespace {

struct Mesh { int id = 0; };

// Loader that installs a mesh after `requiredPumps` update() calls.
class SlowLoader : public threadmaxx::IResourceLoader {
public:
    int pumps = 0;
    int requiredPumps = 3;
    threadmaxx::ResourceId<Mesh> installed{};

    void update(threadmaxx::Engine& engine) override {
        ++pumps;
        if (pumps >= requiredPumps && !installed.valid()) {
            installed = engine.resources().add(Mesh{42});
        }
    }
};

class NeverLoader : public threadmaxx::IResourceLoader {
public:
    int pumps = 0;
    void update(threadmaxx::Engine&) override { ++pumps; }
};

} // namespace

int main() {
    using namespace threadmaxx;

    // ---- happy path: predicate becomes true after a few pumps ------------
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } game;
        CHECK(engine.initialize(game));

        auto* loader = static_cast<SlowLoader*>(
            engine.addResourceLoader(std::make_unique<SlowLoader>()));

        const auto tickBefore = engine.tick();

        const bool ok = engine.preloadUntil(
            [&] { return loader->installed.valid(); },
            std::chrono::milliseconds(1000));
        CHECK(ok);
        CHECK(loader->installed.valid());
        // The predicate is checked once *before* the first pump too, so
        // pumps may equal requiredPumps exactly when done returns true.
        CHECK(loader->pumps >= loader->requiredPumps);
        // Simulation did NOT advance.
        CHECK_EQ(engine.tick(), tickBefore);

        engine.shutdown();
    }

    // ---- timeout returns false; loaders kept pumping in the meantime -----
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } game;
        CHECK(engine.initialize(game));

        auto* loader = static_cast<NeverLoader*>(
            engine.addResourceLoader(std::make_unique<NeverLoader>()));

        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = engine.preloadUntil(
            [] { return false; },
            std::chrono::milliseconds(50));
        const auto elapsed = std::chrono::steady_clock::now() - t0;

        CHECK(!ok);
        CHECK(loader->pumps > 0);
        CHECK(elapsed >= std::chrono::milliseconds(50));
        // Don't assert a strict upper bound — yield-loop timing is fuzzy.

        engine.shutdown();
    }

    // ---- predicate true on entry returns immediately without pumping ----
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } game;
        CHECK(engine.initialize(game));

        auto* loader = static_cast<NeverLoader*>(
            engine.addResourceLoader(std::make_unique<NeverLoader>()));

        const bool ok = engine.preloadUntil([] { return true; });
        CHECK(ok);
        CHECK_EQ(loader->pumps, 0);

        engine.shutdown();
    }

    EXIT_WITH_RESULT();
}
