// §3.11.9 batch D9 — particle storm bench.
//
// Workload: every tick spawn N particles (each carries Transform +
// Velocity + a registered `Particle` user-component) with a known
// lifetime so they auto-destroy ~lifetimeTicks later. After warmup
// the world holds roughly `N * lifetimeTicks` live particles; each
// tick the engine commits ~N spawn commands AND ~N destroy commands.
// That's the workload §3.11.9 calls out as the v1.2 commit-path
// stress that no other bench currently covers.
//
// Scales (particles/sec, with the engine at 60 Hz): 1k / 5k / 25k /
// 100k. Convert to per-tick via `perTick = round(perSec / 60)`. With
// lifetime=30 ticks the 100k row holds ~50k live particles steady-
// state — the upper end of "peak combat" implied by GAME_EXTENSION.md
// §4 batch D9.
//
// CSV columns: see bench/README.md. `note` carries
// `commitHash` (last tick) + average live particle count so a future
// run can spot a determinism regression as well as a wall-clock one.

#include "common.hpp"

#include <threadmaxx/threadmaxx.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/UserComponent.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace threadmaxx;
using namespace threadmaxx_bench;

constexpr std::array<std::uint32_t, 4> kScalesPerSec = {
    1'000, 5'000, 25'000, 100'000};
constexpr int kWarmupTicks = 30;
constexpr int kBenchTicks  = 120;
constexpr std::uint32_t kLifetimeTicks = 30;
constexpr std::uint32_t kTicksPerSec   = 60;

// Same shape as `rpg::Particle`. Kept independent so the bench doesn't
// pull in `rpg_demo_core` (which would drag in GLFW).
struct ParticleBlob {
    float spawnTimeSeconds = 0.0f;
    float initialLifetime  = 0.5f;
    float color[4]         = {1, 1, 1, 1};
    float fadeSeconds      = 0.0f;
    float pad[2]           = {0.0f, 0.0f};
};

class BurstSpawnSystem : public ISystem {
public:
    BurstSpawnSystem(UserComponentId particleId,
                     std::uint32_t   particlesPerTick) noexcept
        : particleId_(particleId),
          perTick_(particlesPerTick),
          rng_(0xD9ABCDEFu) {}

    const char* name() const noexcept override { return "burst-spawn"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::EntityStructural};
    }

    void update(SystemContext& ctx) override {
        std::vector<EntityHandle> handles(perTick_);
        const std::uint32_t got = ctx.reserveHandles(
            perTick_, std::span<EntityHandle>(handles));
        if (got == 0) return;
        handles.resize(got);

        const float life = static_cast<float>(kLifetimeTicks) *
                           static_cast<float>(ctx.dt());
        const float simTime = static_cast<float>(ctx.tick()) *
                              static_cast<float>(ctx.dt());

        std::uniform_real_distribution<float> jitter(-0.5f, 0.5f);
        std::uniform_real_distribution<float> velDist(-3.0f, 3.0f);

        struct Spawn {
            EntityHandle h;
            float px, py, pz;
            float vx, vy, vz;
        };
        std::vector<Spawn> spawns;
        spawns.reserve(got);
        for (std::uint32_t i = 0; i < got; ++i) {
            Spawn s;
            s.h  = handles[i];
            s.px = jitter(rng_); s.py = 5.0f + jitter(rng_); s.pz = jitter(rng_);
            s.vx = velDist(rng_); s.vy = velDist(rng_); s.vz = velDist(rng_);
            spawns.push_back(s);
        }

        const auto id = particleId_;
        ctx.single([spawns = std::move(spawns), id, simTime, life]
                   (Range, CommandBuffer& cb) {
            for (const auto& s : spawns) {
                Bundle b{};
                b.transform.position = {s.px, s.py, s.pz};
                b.transform.scale    = {0.08f, 0.08f, 0.08f};
                b.velocity = Velocity{{s.vx, s.vy, s.vz}, {0,0,0}};
                b.initialMask = ComponentSet{
                    Component::Transform, Component::Velocity,
                };
                cb.spawnBundle(s.h, b);

                ParticleBlob p;
                p.spawnTimeSeconds = simTime;
                p.initialLifetime  = life;
                addUserComponent(cb, id, s.h, p);
            }
        });
    }

private:
    UserComponentId  particleId_;
    std::uint32_t    perTick_;
    std::mt19937     rng_;
};

class AgeOutSystem : public ISystem {
public:
    AgeOutSystem(UserComponentId particleId) noexcept
        : particleId_(particleId) {}

    const char* name() const noexcept override { return "age-out"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }

    void update(SystemContext& ctx) override {
        const auto& w = ctx.world();
        const auto bit = particleId_.componentBit();
        const auto chunks = w.archetypeChunkCount();
        if (chunks == 0) return;
        const float simTime = static_cast<float>(ctx.tick()) *
                              static_cast<float>(ctx.dt());
        std::vector<EntityHandle> expired;
        expired.reserve(1024);
        for (std::size_t c = 0; c < chunks; ++c) {
            const auto& chunk = w.archetypeChunk(c);
            if (!chunk.mask.has(bit)) continue;
            const auto rows = chunk.entities.size();
            if (rows == 0) continue;
            const auto span =
                user::chunkSpan<ParticleBlob>(chunk, particleId_);
            if (span.empty()) continue;
            for (std::size_t r = 0; r < rows; ++r) {
                const auto& p = span[r];
                if (simTime - p.spawnTimeSeconds >= p.initialLifetime) {
                    expired.push_back(chunk.entities[r]);
                }
            }
        }
        if (expired.empty()) return;
        ctx.single([expired = std::move(expired)]
                   (Range, CommandBuffer& cb) {
            for (auto h : expired) cb.destroy(h);
        });
    }

private:
    UserComponentId particleId_;
};

struct BenchGame : IGame {
    UserComponentId particleId;
    std::uint32_t   perTick = 0;

    void onSetup(Engine& eng, World&, CommandBuffer&) override {
        particleId = eng.registerUserComponent<ParticleBlob>();
        eng.registerSystem(std::make_unique<BurstSpawnSystem>(
            particleId, perTick));
        eng.registerSystem(std::make_unique<AgeOutSystem>(particleId));
    }
};

void runScale(CsvWriter& csv, std::uint32_t perSec, std::uint32_t workers) {
    const std::uint32_t perTick =
        (perSec + kTicksPerSec / 2) / kTicksPerSec;  // round-half-up
    if (perTick == 0) return;
    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = workers;
    Engine engine(cfg);

    BenchGame game;
    game.perTick = perTick;
    if (!engine.initialize(game)) {
        std::fprintf(stderr, "engine.initialize failed at N=%u\n", perTick);
        std::exit(2);
    }

    // Warmup so the steady-state population of ~perTick * lifetimeTicks
    // particles is in flight.
    for (int i = 0; i < kWarmupTicks; ++i) engine.step();

    LatencyHistogram hStep;
    LatencyHistogram hCommit;
    std::uint64_t totalCommitNs = 0;
    std::uint64_t lastHash      = 0;
    std::size_t   liveSamples   = 0;
    std::uint64_t liveAccum     = 0;

    hStep.reserve(static_cast<std::size_t>(kBenchTicks));
    hCommit.reserve(static_cast<std::size_t>(kBenchTicks));

    for (int i = 0; i < kBenchTicks; ++i) {
        Stopwatch sw;
        sw.start();
        engine.step();
        hStep.push(sw.elapsedNs());

        const auto& stats = engine.stats();
        const std::uint64_t commitNs = static_cast<std::uint64_t>(
            stats.commitDurationSeconds * 1e9);
        hCommit.push(commitNs);
        totalCommitNs += commitNs;
        lastHash = stats.commitHash;

        liveAccum += engine.world().size();
        ++liveSamples;
    }
    hStep.finalize();
    hCommit.finalize();

    const double meanLive = liveSamples
        ? static_cast<double>(liveAccum) / static_cast<double>(liveSamples)
        : 0.0;
    const double partsPerSec = static_cast<double>(perTick) /
                               (hStep.meanNs() * 1e-9);

    char noteBuf[128];
    std::snprintf(noteBuf, sizeof(noteBuf),
                  "commitHash=0x%016llx live=%.0f commit_us=%.2f",
                  static_cast<unsigned long long>(lastHash),
                  meanLive, hCommit.meanNs() / 1e3);

    BenchRow row{};
    row.label      = "particle_storm";
    row.workload   = "Particles";
    row.entities   = static_cast<std::size_t>(perSec);
    row.workers    = workers;
    row.grain      = 0;
    row.mean_ns    = hStep.meanNs();
    row.stddev     = hStep.stddev();
    row.p50_ns     = hStep.p50Ns();
    row.p95_ns     = hStep.p95Ns();
    row.p99_ns     = hStep.p99Ns();
    row.throughput = partsPerSec;
    row.note       = noteBuf;
    csv.row(row);

    // Mention the commit-only column too so a future reader doesn't
    // have to math `note` apart.
    BenchRow commitRow = row;
    commitRow.label    = "particle_storm_commit";
    commitRow.entities = static_cast<std::size_t>(perSec);
    commitRow.mean_ns  = hCommit.meanNs();
    commitRow.stddev   = hCommit.stddev();
    commitRow.p50_ns   = hCommit.p50Ns();
    commitRow.p95_ns   = hCommit.p95Ns();
    commitRow.p99_ns   = hCommit.p99Ns();
    commitRow.throughput = static_cast<double>(perTick) /
                           (hCommit.meanNs() * 1e-9);
    csv.row(commitRow);

    (void)totalCommitNs;
}

} // namespace

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : nullptr;
    CsvWriter csv(path);

    const std::uint32_t workers = 4;
    for (auto n : kScalesPerSec) {
        runScale(csv, n, workers);
    }
    return 0;
}
