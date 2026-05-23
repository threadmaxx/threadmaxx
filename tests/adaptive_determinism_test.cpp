// ADAPTIVE_TUNING.md T6 — determinism mode + scripted replay.
//
// Three contracts the engine must honor:
//
//   1. `TuningTrace::serialize` / `deserialize` is a clean round-trip.
//      Magic + version + entries survive a stream pass with no
//      drift.
//
//   2. `TuningMode::Active` with an attached trace records every
//      applied `TuningPatch` keyed by the proposing tick. The
//      recorded stream is exactly what the policy emitted.
//
//   3. The determinism guarantee: an `Active`-mode engine + the
//      recorded trace produces a commitHash stream that is bit-
//      identical to a `Scripted`-mode engine replaying the same
//      trace against the same workload.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace threadmaxx;

// Same trivial system shape as `tuning_patch_application_test`:
// `parallelFor` over a fixed count with grain=0 so the engine's
// `preferredGrain` (overridable via TuningPatch) drives sub-job count.
class RecordingSystem : public ISystem {
public:
    const char* name() const noexcept override { return "rec"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }
    void update(SystemContext& ctx) override {
        std::atomic<std::uint32_t> calls{0};
        ctx.parallelFor(kCount, /*grain*/ 0,
            [&calls](Range, CommandBuffer&) {
                calls.fetch_add(1, std::memory_order_relaxed);
            });
        subJobsPerTick.push_back(calls.load(std::memory_order_relaxed));
    }

    static constexpr std::uint32_t kCount = 1000;
    std::vector<std::uint32_t>     subJobsPerTick;
};

// Scripted policy that fires at two distinct ticks so the trace
// contains more than one entry.
class TwoShotPolicy : public ITuningPolicy {
public:
    void observe(const EngineStats& s,
                 std::span<const SystemStats>,
                 const JobSystemStats&) override {
        lastTick_ = s.tick;
    }
    std::optional<TuningPatch> propose() override {
        TuningPatch p;
        if (lastTick_ == 3) {
            p.grainOverrides.push_back({std::string("rec"), 8u});
            return p;
        }
        if (lastTick_ == 5) {
            p.grainOverrides.push_back({std::string("rec"), 32u});
            return p;
        }
        return std::nullopt;
    }
private:
    std::uint64_t lastTick_ = 0;
};

struct NoopGame : public IGame {
    void onSetup(Engine&, World&, CommandBuffer&) override {}
};

void registerRec(Engine& engine, RecordingSystem*& outPtr) {
    auto sys = std::make_unique<RecordingSystem>();
    outPtr   = sys.get();
    engine.registerSystem(std::move(sys));
}

constexpr int kRunTicks = 10;

void runActive(std::vector<std::uint64_t>& hashes,
               TuningTrace&                trace) {
    Config cfg;
    cfg.workerCount = 4;
    cfg.sleepToPace = false;
    Engine engine{cfg};
    NoopGame g;
    engine.initialize(g);
    RecordingSystem* rec = nullptr;
    registerRec(engine, rec);

    TwoShotPolicy p;
    engine.setTuningPolicy(&p);          // implicitly switches to Active
    engine.setTuningTrace(&trace);
    CHECK(engine.tuningMode() == TuningMode::Active);
    CHECK(engine.tuningTrace() == &trace);

    for (int i = 0; i < kRunTicks; ++i) {
        engine.step();
        hashes.push_back(engine.stats().commitHash);
    }
    engine.shutdown();
}

void runScripted(std::vector<std::uint64_t>& hashes,
                 const TuningTrace&          trace) {
    Config cfg;
    cfg.workerCount = 4;
    cfg.sleepToPace = false;
    Engine engine{cfg};
    NoopGame g;
    engine.initialize(g);
    RecordingSystem* rec = nullptr;
    registerRec(engine, rec);

    // Const_cast is intentional: the engine borrows the trace for
    // read-only lookups in Scripted mode. The non-const handle on
    // the public surface keeps Active+record symmetric with
    // Scripted+replay.
    engine.setTuningTrace(const_cast<TuningTrace*>(&trace));
    engine.setTuningMode(TuningMode::Scripted);
    CHECK(engine.tuningMode() == TuningMode::Scripted);
    CHECK(engine.tuningPolicy() == nullptr);

    for (int i = 0; i < kRunTicks; ++i) {
        engine.step();
        hashes.push_back(engine.stats().commitHash);
    }
    engine.shutdown();
}

} // namespace

int main() {
    using namespace threadmaxx;

    // ----- (1) TuningTrace serialize/deserialize round-trip ---------
    {
        TuningTrace trace;
        TuningPatch p1;
        p1.grainOverrides.push_back({std::string("alpha"), 64u});
        p1.grainOverrides.push_back({std::string("bravo"), 128u});
        trace.record(7, p1);

        TuningPatch p2;
        p2.grainOverrides.push_back({std::string("charlie"), 0u});
        trace.record(11, p2);

        std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
        trace.serialize(ss);
        ss.seekg(0);
        TuningTrace round = TuningTrace::deserialize(ss);
        CHECK_EQ(round.size(), std::size_t{2});

        TuningPatch out;
        CHECK(round.tryGet(7, out));
        CHECK_EQ(out.grainOverrides.size(), std::size_t{2});
        CHECK_EQ(out.grainOverrides[0].systemName, std::string("alpha"));
        CHECK_EQ(out.grainOverrides[0].preferredGrain, std::uint32_t{64});
        CHECK_EQ(out.grainOverrides[1].systemName, std::string("bravo"));
        CHECK_EQ(out.grainOverrides[1].preferredGrain, std::uint32_t{128});

        CHECK(round.tryGet(11, out));
        CHECK_EQ(out.grainOverrides.size(), std::size_t{1});
        CHECK_EQ(out.grainOverrides[0].systemName, std::string("charlie"));
        CHECK_EQ(out.grainOverrides[0].preferredGrain, std::uint32_t{0});

        CHECK(!round.tryGet(99, out));

        std::printf("[adaptive_determinism/round_trip] entries=%zu\n",
                    round.size());
    }

    // ----- (2) deserialize rejects bad input ------------------------
    {
        std::stringstream junk(std::string("\x00\x00\x00\x00", 4),
                               std::ios::in | std::ios::binary);
        TuningTrace t = TuningTrace::deserialize(junk);
        CHECK(t.empty());

        std::stringstream truncated(std::string("\x54\x55\x4E\x45", 4),
                                    std::ios::in | std::ios::binary);
        TuningTrace t2 = TuningTrace::deserialize(truncated);
        CHECK(t2.empty());
    }

    // ----- (3) Active mode records every applied patch --------------
    TuningTrace recorded;
    std::vector<std::uint64_t> hashesActive;
    runActive(hashesActive, recorded);

    // TwoShotPolicy fires at tick 3 and tick 5 → exactly 2 entries.
    CHECK_EQ(recorded.size(), std::size_t{2});
    TuningPatch out;
    CHECK(recorded.tryGet(3, out));
    CHECK_EQ(out.grainOverrides.size(), std::size_t{1});
    CHECK_EQ(out.grainOverrides[0].systemName, std::string("rec"));
    CHECK_EQ(out.grainOverrides[0].preferredGrain, std::uint32_t{8});
    CHECK(recorded.tryGet(5, out));
    CHECK_EQ(out.grainOverrides[0].preferredGrain, std::uint32_t{32});

    // ----- (4) Replay via serialize → deserialize → Scripted run ----
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    recorded.serialize(ss);
    ss.seekg(0);
    TuningTrace replay = TuningTrace::deserialize(ss);
    CHECK_EQ(replay.size(), recorded.size());

    std::vector<std::uint64_t> hashesScripted;
    runScripted(hashesScripted, replay);

    // The determinism guarantee.
    CHECK_EQ(hashesActive.size(), std::size_t{kRunTicks});
    CHECK_EQ(hashesActive.size(), hashesScripted.size());
    for (std::size_t i = 0; i < hashesActive.size(); ++i) {
        CHECK_EQ(hashesActive[i], hashesScripted[i]);
    }

    std::printf("[adaptive_determinism/replay] ticks=%zu hash=0x%016llx\n",
                hashesActive.size(),
                static_cast<unsigned long long>(hashesActive.back()));

    // ----- (5) Scripted mode without a trace is inert ---------------
    {
        Config cfg;
        cfg.sleepToPace = false;
        Engine engine{cfg};
        NoopGame g;
        engine.initialize(g);
        engine.setTuningMode(TuningMode::Scripted);
        // No trace; no policy. Engine must still step cleanly.
        engine.step();
        engine.step();
        engine.shutdown();
    }

    EXIT_WITH_RESULT();
}
