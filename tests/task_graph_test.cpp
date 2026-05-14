// §3.4 batch 11: frame task graph.
//
// Exercises the DAG-aware wave scheduler:
//   - Pure read/write conflict scheduling is unchanged (backwards
//     compat).
//   - `provides()` / `dependencies()` tags introduce extra edges that
//     can push a consumer into a later wave even when read/write masks
//     alone would have placed it in the same wave as the producer.
//   - `preferredGrain()` overrides the default chunking heuristic when
//     a `parallelFor` call passes `grain=0`.
//   - `taskGraphSnapshot()` returns one row per registered system with
//     correct wave assignment and predecessor indices.
//   - Cycle detection logs a warning via ILogger and recovers (no
//     crash, no hang) by dropping the offending tag-only edges.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace {

class EmptyGame : public threadmaxx::IGame {
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {}
};

// A simple writer system. Declares it writes Health, optionally
// declares a `provides` tag.
class HealthWriter : public threadmaxx::ISystem {
public:
    static constexpr threadmaxx::TaskTag kHealthReady{"health-ready"};
    bool emitProvide = false;
    std::atomic<int>* runOrder = nullptr;
    int*              recordedOrder = nullptr;
    const char* name() const noexcept override { return "health-writer"; }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Health};
    }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    std::span<const threadmaxx::TaskTag> provides() const noexcept override {
        return emitProvide ? std::span<const threadmaxx::TaskTag>(&kHealthReady, 1)
                           : std::span<const threadmaxx::TaskTag>();
    }
    void update(threadmaxx::SystemContext&) override {
        if (runOrder && recordedOrder) *recordedOrder = runOrder->fetch_add(1);
    }
};

// A reader system. Reads Faction (so no rw-conflict with HealthWriter),
// optionally declares it depends on "health-ready".
class FactionReader : public threadmaxx::ISystem {
public:
    static constexpr threadmaxx::TaskTag kHealthReady{"health-ready"};
    bool depend = false;
    std::atomic<int>* runOrder = nullptr;
    int*              recordedOrder = nullptr;
    const char* name() const noexcept override { return "faction-reader"; }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Faction};
    }
    std::span<const threadmaxx::TaskTag> dependencies() const noexcept override {
        return depend ? std::span<const threadmaxx::TaskTag>(&kHealthReady, 1)
                      : std::span<const threadmaxx::TaskTag>();
    }
    void update(threadmaxx::SystemContext&) override {
        if (runOrder && recordedOrder) *recordedOrder = runOrder->fetch_add(1);
    }
};

// System that exercises preferredGrain: records the per-chunk range
// size it actually got.
class GrainProbe : public threadmaxx::ISystem {
public:
    std::uint32_t hint = 0;
    std::mutex mtx;
    std::vector<std::uint32_t> chunkSizes;
    bool done = false;
    const char* name() const noexcept override { return "grain-probe"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    std::uint32_t preferredGrain() const noexcept override { return hint; }
    void update(threadmaxx::SystemContext& ctx) override {
        if (done) return;
        done = true;
        ctx.parallelFor(1000, /*grain=*/0,
            [this](threadmaxx::Range r, threadmaxx::CommandBuffer&) {
                std::lock_guard<std::mutex> lk(mtx);
                chunkSizes.push_back(r.size());
            });
    }
};

// Logger that records every Warn+ message.
class CapturingLogger : public threadmaxx::ILogger {
public:
    std::mutex mtx;
    std::vector<std::string> warnings;
    void log(threadmaxx::LogLevel level, std::string_view msg) override {
        if (level >= threadmaxx::LogLevel::Warn) {
            std::lock_guard<std::mutex> lk(mtx);
            warnings.emplace_back(msg);
        }
    }
};

// Two systems that depend on each other's provides — pure 2-node cycle
// in the dep graph. Out-of-main so they can carry static TaskTag members.
class CycleA : public threadmaxx::ISystem {
public:
    static constexpr threadmaxx::TaskTag kX{"cycle-X"};
    static constexpr threadmaxx::TaskTag kY{"cycle-Y"};
    const char* name() const noexcept override { return "cycle-a"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    std::span<const threadmaxx::TaskTag> provides() const noexcept override {
        return {&kX, 1};
    }
    std::span<const threadmaxx::TaskTag> dependencies() const noexcept override {
        return {&kY, 1};
    }
    void update(threadmaxx::SystemContext&) override {}
};
class CycleB : public threadmaxx::ISystem {
public:
    static constexpr threadmaxx::TaskTag kX{"cycle-X"};
    static constexpr threadmaxx::TaskTag kY{"cycle-Y"};
    const char* name() const noexcept override { return "cycle-b"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    std::span<const threadmaxx::TaskTag> provides() const noexcept override {
        return {&kY, 1};
    }
    std::span<const threadmaxx::TaskTag> dependencies() const noexcept override {
        return {&kX, 1};
    }
    void update(threadmaxx::SystemContext&) override {}
};

} // namespace

int main() {
    using namespace threadmaxx;

    // ---- Test 1: backward-compat. No tags = first-fit by read/write
    //              like before. HealthWriter (writes Health) and
    //              FactionReader (reads Faction) share wave 0.
    {
        Config cfg;
        cfg.sleepToPace = false;
        cfg.workerCount = 2;
        Engine engine(cfg);
        EmptyGame game;
        CHECK(engine.initialize(game));
        engine.registerSystem(std::make_unique<HealthWriter>());
        engine.registerSystem(std::make_unique<FactionReader>());
        const auto snap = engine.taskGraphSnapshot();
        CHECK_EQ(snap.size(), std::size_t{2});
        CHECK_EQ(snap[0].wave, std::size_t{0});
        CHECK_EQ(snap[1].wave, std::size_t{0});
        CHECK(snap[0].dependsOn.empty());
        CHECK(snap[1].dependsOn.empty());
        engine.shutdown();
    }

    // ---- Test 2: tag dependency pushes consumer into later wave even
    //              when read/write masks alone wouldn't.
    {
        Config cfg;
        cfg.sleepToPace = false;
        cfg.workerCount = 2;
        Engine engine(cfg);
        EmptyGame game;
        CHECK(engine.initialize(game));
        auto writer = std::make_unique<HealthWriter>();
        writer->emitProvide = true;
        auto reader = std::make_unique<FactionReader>();
        reader->depend = true;

        std::atomic<int> order{0};
        int writerOrder = -1, readerOrder = -1;
        writer->runOrder = &order;
        writer->recordedOrder = &writerOrder;
        reader->runOrder = &order;
        reader->recordedOrder = &readerOrder;

        engine.registerSystem(std::move(writer));
        engine.registerSystem(std::move(reader));
        const auto snap = engine.taskGraphSnapshot();
        CHECK_EQ(snap.size(), std::size_t{2});
        CHECK_EQ(snap[0].name, std::string("health-writer"));
        CHECK_EQ(snap[1].name, std::string("faction-reader"));
        CHECK_EQ(snap[0].wave, std::size_t{0});
        CHECK_EQ(snap[1].wave, std::size_t{1});
        CHECK_EQ(snap[1].dependsOn.size(), std::size_t{1});
        CHECK_EQ(snap[1].dependsOn[0], std::size_t{0});

        engine.step();
        CHECK(writerOrder >= 0);
        CHECK(readerOrder >= 0);
        CHECK(writerOrder < readerOrder);
        engine.shutdown();
    }

    // ---- Test 3: preferredGrain is honored when caller passes grain=0.
    {
        Config cfg;
        cfg.sleepToPace = false;
        cfg.workerCount = 4;
        Engine engine(cfg);
        EmptyGame game;
        CHECK(engine.initialize(game));
        auto probe = std::make_unique<GrainProbe>();
        probe->hint = 250;
        auto* probePtr = probe.get();
        engine.registerSystem(std::move(probe));
        engine.step();
        // 1000 items, grain 250 → 4 chunks of size 250 each.
        std::lock_guard<std::mutex> lk(probePtr->mtx);
        CHECK_EQ(probePtr->chunkSizes.size(), std::size_t{4});
        for (auto s : probePtr->chunkSizes) CHECK_EQ(s, std::uint32_t{250});
        engine.shutdown();
    }

    // ---- Test 4: cycle detection logs + recovers.
    //              A provides "X" + depends on "Y"; B provides "Y" +
    //              depends on "X". This is a 2-node cycle; the engine
    //              should log a warning and still run.
    {
        Config cfg;
        cfg.sleepToPace = false;
        cfg.workerCount = 2;
        Engine engine(cfg);
        CapturingLogger logger;
        engine.setLogger(&logger);
        EmptyGame game;
        CHECK(engine.initialize(game));
        engine.registerSystem(std::make_unique<CycleA>());
        engine.registerSystem(std::make_unique<CycleB>());

        // At least one cycle warning was emitted.
        bool sawCycleMessage = false;
        {
            std::lock_guard<std::mutex> lk(logger.mtx);
            for (const auto& w : logger.warnings) {
                if (w.find("cycle") != std::string::npos) {
                    sawCycleMessage = true;
                    break;
                }
            }
        }
        CHECK(sawCycleMessage);

        // Engine still runs without hanging.
        engine.step();
        engine.step();
        engine.shutdown();
    }

    // ---- Test 5: preferredGrain == 0 (default) still uses the
    //              4-chunks-per-worker heuristic.
    {
        Config cfg;
        cfg.sleepToPace = false;
        cfg.workerCount = 4;
        Engine engine(cfg);
        EmptyGame game;
        CHECK(engine.initialize(game));
        auto probe = std::make_unique<GrainProbe>();
        probe->hint = 0;   // explicit default
        auto* probePtr = probe.get();
        engine.registerSystem(std::move(probe));
        engine.step();
        // 1000 items, 4 workers, target 16 chunks → grain = 63.
        // Number of chunks should be ceil(1000/63) = 16.
        std::lock_guard<std::mutex> lk(probePtr->mtx);
        CHECK_EQ(probePtr->chunkSizes.size(), std::size_t{16});
        engine.shutdown();
    }

    EXIT_WITH_RESULT();
}
