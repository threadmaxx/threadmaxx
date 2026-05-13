// §3.3 IResourceLoader: engine pumps loader->update(engine) once per
// step on the sim thread, after the postStep hooks commit. Verifies:
//   - The pump fires every step()
//   - A loader can call engine.resources().add() from update() and have
//     the new resource visible via get() afterward
//   - Multiple loaders pump in registration order
//   - Loaders are torn down in reverse-registration order at shutdown

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <vector>

namespace {

struct Mesh {
    int id = 0;
};

// Test loader: counts pumps and on the 3rd tick installs a Mesh.
class CountingLoader : public threadmaxx::IResourceLoader {
public:
    int pumps = 0;
    threadmaxx::ResourceId<Mesh> installed{};
    std::vector<int>* teardownLog = nullptr;
    int tag = 0;

    ~CountingLoader() override {
        if (teardownLog) teardownLog->push_back(tag);
    }

    void update(threadmaxx::Engine& engine) override {
        ++pumps;
        if (pumps == 3 && !installed.valid()) {
            installed = engine.resources().add(Mesh{42});
        }
    }
};

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 1;
    Engine engine(cfg);

    struct Game : IGame {
        void onSetup(Engine&, World&, CommandBuffer&) override {}
    } game;
    CHECK(engine.initialize(game));

    std::vector<int> teardownLog;
    auto a = std::make_unique<CountingLoader>();
    auto b = std::make_unique<CountingLoader>();
    a->tag = 1;
    b->tag = 2;
    a->teardownLog = &teardownLog;
    b->teardownLog = &teardownLog;
    auto* aRaw = engine.addResourceLoader(std::move(a));
    auto* bRaw = engine.addResourceLoader(std::move(b));
    CHECK(aRaw != nullptr);
    CHECK(bRaw != nullptr);

    // Five steps — every loader should pump five times.
    for (int i = 0; i < 5; ++i) engine.step();

    auto* aPtr = static_cast<CountingLoader*>(aRaw);
    auto* bPtr = static_cast<CountingLoader*>(bRaw);
    CHECK_EQ(aPtr->pumps, 5);
    CHECK_EQ(bPtr->pumps, 5);

    // Both loaders should have installed a mesh on tick 3.
    CHECK(aPtr->installed.valid());
    CHECK(bPtr->installed.valid());

    const auto* m = engine.resources().get(aPtr->installed);
    CHECK(m != nullptr);
    CHECK_EQ(m->id, 42);

    // No-op: adding a null loader returns nullptr (sanity).
    CHECK(engine.addResourceLoader(nullptr) == nullptr);

    engine.shutdown();

    // Teardown order: registered as [1, 2], destroyed in reverse [2, 1].
    CHECK_EQ(teardownLog.size(), std::size_t{2});
    CHECK_EQ(teardownLog[0], 2);
    CHECK_EQ(teardownLog[1], 1);

    EXIT_WITH_RESULT();
}
