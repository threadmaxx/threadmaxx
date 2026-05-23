// ADAPTIVE_TUNING.md T2 — ISystem::preferredWorkerCap() clamps the
// per-system parallelFor fan-out. We assert four things:
//
//   1. Default cap (return 0) → no clamp; behaves like before.
//   2. A system returning cap=K sees at most K sub-jobs even when
//      grain=0 + count >> workerCount*4 would otherwise pick more.
//   3. The cap also clamps an explicit caller-supplied grain that
//      would have produced more sub-jobs.
//   4. Determinism: toggling the cap does NOT change commitHash for
//      the same input command stream (commit order is by submission
//      index, not chunk count).
//
// The harness sets workerCount=8 so the uncapped path would fan out
// to ~32 sub-jobs by B28's heuristic; the capped path should land
// at the declared cap.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>

namespace {

class CountedFanoutSystem : public threadmaxx::ISystem {
public:
    // count       : per-tick parallelFor count.
    // explicitGrain: 0 → let engine pick; non-zero → caller-supplied.
    // cap         : preferredWorkerCap() return value.
    CountedFanoutSystem(const char* n,
                        std::uint32_t count,
                        std::uint32_t explicitGrain,
                        std::uint32_t cap)
        : name_(n), count_(count), grain_(explicitGrain), cap_(cap) {}

    const char* name() const noexcept override { return name_; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    std::uint32_t preferredWorkerCap() const noexcept override { return cap_; }

    void update(threadmaxx::SystemContext& ctx) override {
        subJobCount_.store(0, std::memory_order_relaxed);
        ctx.parallelFor(count_, grain_,
            [this](threadmaxx::Range, threadmaxx::CommandBuffer&) {
                subJobCount_.fetch_add(1, std::memory_order_relaxed);
            });
    }

    std::uint32_t lastSubJobCount() const noexcept {
        return subJobCount_.load(std::memory_order_relaxed);
    }

private:
    const char*               name_;
    std::uint32_t             count_;
    std::uint32_t             grain_;
    std::uint32_t             cap_;
    std::atomic<std::uint32_t> subJobCount_{0};
};

class CapHarnessGame : public threadmaxx::IGame {
public:
    CountedFanoutSystem* uncapped = nullptr;
    CountedFanoutSystem* capped   = nullptr;
    CountedFanoutSystem* capOverridesExplicitGrain = nullptr;

    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {
        auto a = std::make_unique<CountedFanoutSystem>(
            "uncapped", /*count*/ 10000, /*grain*/ 0, /*cap*/ 0);
        uncapped = a.get();
        engine.registerSystem(std::move(a));

        auto b = std::make_unique<CountedFanoutSystem>(
            "capped", /*count*/ 10000, /*grain*/ 0, /*cap*/ 4);
        capped = b.get();
        engine.registerSystem(std::move(b));

        // grain=10 → would-be 1000 sub-jobs; cap=4 must clamp it.
        auto c = std::make_unique<CountedFanoutSystem>(
            "cap-over-explicit", /*count*/ 10000, /*grain*/ 10, /*cap*/ 4);
        capOverridesExplicitGrain = c.get();
        engine.registerSystem(std::move(c));
    }
};

} // namespace

int main() {
    // --- Test 1+2+3: counted sub-job dispatch -----------------
    {
        threadmaxx::Config cfg;
        cfg.workerCount = 8;
        cfg.sleepToPace = false;
        threadmaxx::Engine engine(cfg);
        CapHarnessGame game;
        CHECK(engine.initialize(game));
        engine.step();

        // Uncapped: pickGrain target = workers*4 = 32, count=10000 →
        // grain = ceil(10000/32) = 313, chunkCount = ceil(10000/313)
        //   = 32. So 32 sub-jobs. The peak-queue-depth assertion is
        // hardware-dependent; assert just on count.
        CHECK_EQ(game.uncapped->lastSubJobCount(), 32u);

        // Capped at 4: still 4 sub-jobs regardless of count.
        CHECK_EQ(game.capped->lastSubJobCount(), 4u);

        // Caller-supplied grain=10 would have produced 1000 sub-jobs;
        // cap=4 clamps it to 4.
        CHECK_EQ(game.capOverridesExplicitGrain->lastSubJobCount(), 4u);

        engine.shutdown();
    }

    // --- Test 4: determinism — cap doesn't change commitHash ----
    // The system writes via CommandBuffer-less callbacks (just counts),
    // so commitHash is the empty-hash basis. To make the test
    // meaningful, use a system that DOES emit commands.
    {
        // Build a custom mini-system that emits a fixed command per
        // sub-job. Same total commands regardless of chunk count
        // because each parallelFor call receives the same Range; the
        // engine splits them into per-job slices but the overall set
        // of (begin,end) intervals partitions [0, count) the same way
        // — actually the partition CHANGES with chunk count, but the
        // total set of commands emitted is determined by the lambda
        // body (which writes once per call here). To keep the test
        // independent of partitioning, use a writer that emits one
        // command per *row* (Range::size()) — count is invariant.
        class RowWriter : public threadmaxx::ISystem {
        public:
            explicit RowWriter(std::uint32_t cap) : cap_(cap) {}
            const char* name() const noexcept override { return "row-writer"; }
            std::uint32_t preferredWorkerCap() const noexcept override { return cap_; }
            void update(threadmaxx::SystemContext& ctx) override {
                const auto entities = ctx.world().entities();
                if (entities.empty()) return;
                const auto e = entities[0];
                ctx.parallelFor(64, /*grain*/ 0,
                    [e](threadmaxx::Range r, threadmaxx::CommandBuffer& cb) {
                        for (std::uint32_t i = r.begin; i < r.end; ++i) {
                            cb.setUserData(e, threadmaxx::UserData{i});
                        }
                    });
            }
        private:
            std::uint32_t cap_;
        };

        class WriterGame : public threadmaxx::IGame {
        public:
            std::uint32_t cap = 0;
            void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                         threadmaxx::CommandBuffer& seed) override {
                seed.spawn({});
                engine.registerSystem(std::make_unique<RowWriter>(cap));
            }
        };

        threadmaxx::Config cfg;
        cfg.workerCount = 8;
        cfg.sleepToPace = false;
        cfg.legacyCommitHash = false;

        // Run A: uncapped (cap=0).
        std::uint64_t hashA = 0;
        {
            threadmaxx::Engine engine(cfg);
            WriterGame game;
            game.cap = 0;
            CHECK(engine.initialize(game));
            for (int i = 0; i < 5; ++i) engine.step();
            hashA = engine.stats().commitHash;
            engine.shutdown();
        }
        // Run B: cap=2 (much smaller fan-out → different partitioning,
        // but same total UserData writes ordered by submission index).
        std::uint64_t hashB = 0;
        {
            threadmaxx::Engine engine(cfg);
            WriterGame game;
            game.cap = 2;
            CHECK(engine.initialize(game));
            for (int i = 0; i < 5; ++i) engine.step();
            hashB = engine.stats().commitHash;
            engine.shutdown();
        }
        CHECK_EQ(hashA, hashB);
    }

    EXIT_WITH_RESULT();
}
