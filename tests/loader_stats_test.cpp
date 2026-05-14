// §3.2 batch 7: LoaderStats surface + Engine::aggregateLoaderStats sums
// every registered loader. Default impl returns zeros.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

namespace {

class StatLoader : public threadmaxx::IResourceLoader {
public:
    threadmaxx::LoaderStats current{};
    void update(threadmaxx::Engine&) override {}
    threadmaxx::LoaderStats stats() const noexcept override { return current; }
};

class DefaultLoader : public threadmaxx::IResourceLoader {
public:
    void update(threadmaxx::Engine&) override {}
};

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
    Engine engine(cfg);
    struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } game;
    CHECK(engine.initialize(game));

    // Empty registry: zero aggregate.
    {
        const auto agg = engine.aggregateLoaderStats();
        CHECK_EQ(agg.pendingLoads,    std::uint64_t{0});
        CHECK_EQ(agg.inFlight,        std::uint64_t{0});
        CHECK_EQ(agg.ready,           std::uint64_t{0});
        CHECK_EQ(agg.failed,          std::uint64_t{0});
        CHECK_EQ(agg.memoryFootprint, std::uint64_t{0});
        CHECK_EQ(agg.memoryBudget,    std::uint64_t{0});
    }

    auto* a = static_cast<StatLoader*>(
        engine.addResourceLoader(std::make_unique<StatLoader>()));
    auto* b = static_cast<StatLoader*>(
        engine.addResourceLoader(std::make_unique<StatLoader>()));
    engine.addResourceLoader(std::make_unique<DefaultLoader>()); // contributes zeros

    a->current = LoaderStats{
        .pendingLoads = 3, .inFlight = 1, .ready = 5, .failed = 2,
        .memoryFootprint = 100, .memoryBudget = 1000,
    };
    b->current = LoaderStats{
        .pendingLoads = 7, .inFlight = 4, .ready = 0, .failed = 1,
        .memoryFootprint = 50, .memoryBudget = 200,
    };

    const auto agg = engine.aggregateLoaderStats();
    CHECK_EQ(agg.pendingLoads,    std::uint64_t{10});
    CHECK_EQ(agg.inFlight,        std::uint64_t{5});
    CHECK_EQ(agg.ready,           std::uint64_t{5});
    CHECK_EQ(agg.failed,          std::uint64_t{3});
    CHECK_EQ(agg.memoryFootprint, std::uint64_t{150});
    CHECK_EQ(agg.memoryBudget,    std::uint64_t{1200});

    engine.shutdown();
    EXIT_WITH_RESULT();
}
