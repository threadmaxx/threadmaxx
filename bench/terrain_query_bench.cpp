// §3.11.8 batch D8 — terrain query bench.
//
// Measures the cost of querying the D8 heightmap (`heightAt` /
// `slopeAt`) at scale, comparing three dispatch shapes:
//
//   1. **single_threaded** — tight scalar loop on the sim thread. No
//      engine, no jobs. Sets the floor: any work above this is
//      overhead.
//   2. **forEachChunk_parallel** — real engine with N entities (1
//      Transform + Velocity each), running a `forEachChunk` system
//      that calls `heightAt` per row. Measures the engine path's
//      parallel-dispatch overhead at terrain-query intensity.
//   3. **slope_query** — same shape as (2) but calling `slopeAt`
//      (which internally does 4 heightAt calls). Captures the
//      gameplay path NPCBrainSystem takes in its Wander target
//      picker.
//
// Scales: 16k / 64k / 256k entities — matching the 16k / 64k / 256k
// cell counts called out in `GAME_EXTENSION.md §4 Batch D8`. The 256k
// row hits stress mode's terrain population.
//
// CSV columns follow the standard §3.9 schema (see `bench/README.md`).
// `note` carries `ns_per_query`.

#include "common.hpp"
#include "Heightmap.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace threadmaxx;
using namespace threadmaxx_bench;

constexpr std::uint32_t kHeightmapResolution = 128u;
constexpr float         kTerrainExtent       = 256.0f;
constexpr std::uint32_t kHeightmapSeed       = 0xD8000001u;

constexpr std::array<std::size_t, 3> kScales = {16'000, 64'000, 256'000};

constexpr int kWarmupIters = 3;
constexpr int kBenchIters  = 12;

// Build N entities each carrying Transform + Velocity at random
// positions inside the heightmap's bounds.
void seedEntities(World&, CommandBuffer& cb, std::size_t n) {
    std::mt19937 rng(0xDA1A1234u);
    std::uniform_real_distribution<float> px(-kTerrainExtent * 0.5f,
                                              +kTerrainExtent * 0.5f);
    for (std::size_t i = 0; i < n; ++i) {
        Bundle b{};
        b.transform.position = {px(rng), 0.0f, px(rng)};
        b.transform.scale    = {1.0f, 1.0f, 1.0f};
        b.velocity           = Velocity{{0.5f, 0.0f, 0.5f}, {0,0,0}};
        b.initialMask        = ComponentSet{
            Component::Transform,
            Component::Velocity,
        };
        cb.spawn(b.transform, b.velocity, b.renderTag, b.userData,
                 b.acceleration, b.parent, b.initialMask);
    }
}

// Game-side system that calls heightAt per entity. Stand-in for
// `TerrainAttachSystem`'s inner loop; doesn't write back, so the
// command buffer stays empty.
class HeightQuerySystem : public ISystem {
public:
    HeightQuerySystem(const rpg::Heightmap* hmap, bool slope) noexcept
        : hmap_(hmap), slope_(slope) {}
    const char* name() const noexcept override { return "height-query"; }
    ComponentSet reads()  const noexcept override {
        return ComponentSet{Component::Transform};
    }
    ComponentSet writes() const noexcept override {
        return ComponentSet::none();
    }
    void update(SystemContext& ctx) override {
        const auto* hmap = hmap_;
        const bool slope = slope_;
        forEachChunk<Transform>(ctx,
            [hmap, slope](std::span<const EntityHandle>,
                          std::span<const Transform> ts,
                          CommandBuffer&) {
                double s = 0.0;
                for (const auto& t : ts) {
                    if (slope) {
                        s += hmap->slopeAt(t.position.x, t.position.z);
                    } else {
                        s += hmap->heightAt(t.position.x, t.position.z);
                    }
                }
                // Force the compiler to keep the work alive.
                volatile double sink = s;
                (void)sink;
            });
    }
private:
    const rpg::Heightmap* hmap_;
    bool                  slope_;
};

void runSingleThreaded(CsvWriter& csv, const rpg::Heightmap& hmap,
                       std::size_t n, std::uint32_t workers, bool slope) {
    std::mt19937 rng(0xDA1A1234u);
    std::uniform_real_distribution<float> px(-kTerrainExtent * 0.5f,
                                              +kTerrainExtent * 0.5f);
    std::vector<float> xs(n), zs(n);
    for (std::size_t i = 0; i < n; ++i) {
        xs[i] = px(rng);
        zs[i] = px(rng);
    }

    LatencyHistogram h;
    runIters(h, kWarmupIters, kBenchIters, [&]() {
        double s = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            s += slope ? hmap.slopeAt(xs[i], zs[i])
                       : hmap.heightAt(xs[i], zs[i]);
        }
        volatile double sink = s;
        (void)sink;
    });

    BenchRow row{};
    row.label    = slope ? std::string("slope_single_threaded")
                         : std::string("height_single_threaded");
    row.workload = "Terrain";
    row.entities = n;
    row.workers  = workers;
    row.grain    = 0;
    row.mean_ns  = h.meanNs();
    row.stddev   = h.stddev();
    row.p50_ns   = h.p50Ns();
    row.p95_ns   = h.p95Ns();
    row.p99_ns   = h.p99Ns();
    const double nsPerQuery = h.meanNs() / static_cast<double>(n);
    row.throughput = static_cast<double>(n) / (h.meanNs() * 1e-9);
    char note[64];
    std::snprintf(note, sizeof(note), "ns/query=%.2f", nsPerQuery);
    row.note     = note;
    csv.row(row);
}

void runParallel(CsvWriter& csv, const rpg::Heightmap& hmap,
                 std::size_t n, std::uint32_t workers, bool slope) {
    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = workers;
    Engine engine(cfg);

    struct SeedGame : IGame {
        std::size_t        n;
        const rpg::Heightmap* hmap;
        bool               slope;
        void onSetup(Engine& eng, World& w, CommandBuffer& cb) override {
            seedEntities(w, cb, n);
            eng.registerSystem(std::make_unique<HeightQuerySystem>(hmap, slope));
        }
    } game;
    game.n     = n;
    game.hmap  = &hmap;
    game.slope = slope;
    if (!engine.initialize(game)) {
        std::fprintf(stderr, "engine.initialize failed at n=%zu\n", n);
        std::exit(2);
    }

    LatencyHistogram h;
    runIters(h, kWarmupIters, kBenchIters, [&]() { engine.step(); });

    BenchRow row{};
    row.label    = slope ? std::string("slope_forEachChunk")
                         : std::string("height_forEachChunk");
    row.workload = "Terrain";
    row.entities = n;
    row.workers  = workers;
    row.grain    = 0;
    row.mean_ns  = h.meanNs();
    row.stddev   = h.stddev();
    row.p50_ns   = h.p50Ns();
    row.p95_ns   = h.p95Ns();
    row.p99_ns   = h.p99Ns();
    const double nsPerQuery = h.meanNs() / static_cast<double>(n);
    row.throughput = static_cast<double>(n) / (h.meanNs() * 1e-9);
    char note[64];
    std::snprintf(note, sizeof(note), "ns/query=%.2f", nsPerQuery);
    row.note     = note;
    csv.row(row);
}

} // namespace

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : nullptr;
    CsvWriter csv(path);

    rpg::Heightmap hmap(kHeightmapResolution, kTerrainExtent, kHeightmapSeed);
    std::fprintf(stderr,
                 "[terrain_query_bench] heightmap res=%u extent=%.1f "
                 "min=%.2f max=%.2f\n",
                 hmap.resolution(), hmap.worldExtent(),
                 hmap.minHeight(), hmap.maxHeight());

    const std::uint32_t workers = 4;
    for (auto n : kScales) {
        runSingleThreaded(csv, hmap, n, /*workers*/ 1u, /*slope*/ false);
        runSingleThreaded(csv, hmap, n, /*workers*/ 1u, /*slope*/ true);
        runParallel       (csv, hmap, n, workers,         /*slope*/ false);
        runParallel       (csv, hmap, n, workers,         /*slope*/ true);
    }
    return 0;
}
