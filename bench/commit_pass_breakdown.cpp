// SHARDED_OPTIMISATION.md S0 — sharded commit Pass A / Pass B / Pass C
// wall-clock breakdown.
//
// Reads `Engine::lastCommitBreakdown()` after every `step()` and emits
// one JSON-Lines row per tick (path × workload × tick) plus a summary
// table on stdout. Every post-S0 batch in `SHARDED_OPTIMISATION.md`
// reads this output to decide whether Pass A, B, or C dominates and
// where the next batch should aim.
//
// Workloads measured (§4 of the plan):
//   - setTransform/Churn   (100k value-only, single archetype)
//   - setVelocity/Churn    (100k value-only, single archetype)
//   - addRemoveTag/Churn   (100k mask-flip, single archetype)
//   - spawnDestroy/Churn   (1k spawn + 1k destroy, sparse global)
//   - setTransform/MultiArch (100k value-only across 4 archetypes)
//   - RPG-mix              (~100k entities, 5 archetypes, value-heavy)
//   - SmallWorld           (256 entities — must keep auto-fallthrough)
//
// Each is run with `singleThreadedCommit ∈ {true, false}`. The
// breakdown only fills with non-zero Pass A/B/C wall-clock when the
// sharded path is taken AND avoids early-out; `fallback_calls` and
// `nsTotal` cover the rest. SmallWorld and the all-migrating
// `addRemoveTag` are expected to hit `fallback_calls > 0`; that's the
// designed-in behaviour and any post-S0 batch must preserve it.
//
// Usage:
//   ./commit_pass_breakdown                                  # stdout summary
//   ./commit_pass_breakdown breakdown.jsonl                  # JSONL + summary

#include "common.hpp"
#include "scene_workloads.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace threadmaxx;
using namespace threadmaxx_bench;

// -----------------------------------------------------------------------------
// Per-tick command emitters. Each one mirrors a shape from §4 of
// SHARDED_OPTIMISATION.md; values intentionally match the existing
// `commit_path_bench.cpp` patterns so the two benches are directly
// comparable (this bench adds the per-pass breakdown that
// `commit_path_bench.cpp` does not record).

class SetTransformChurn : public ISystem {
public:
    explicit SetTransformChurn(std::vector<EntityHandle> es)
        : entities_(std::move(es)) {}
    const char* name() const noexcept override { return "setTransform"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Transform};
    }
    void update(SystemContext& ctx) override {
        const auto t = ctx.tick();
        ctx.parallelFor(static_cast<std::uint32_t>(entities_.size()), 256,
            [this, t](Range r, CommandBuffer& cb) {
                for (auto i = r.begin; i < r.end; ++i) {
                    Transform tr{};
                    tr.position.x = static_cast<float>((i + t) & 0xFF);
                    cb.setTransform(entities_[i], tr);
                }
            });
    }
private:
    std::vector<EntityHandle> entities_;
};

class SetVelocityChurn : public ISystem {
public:
    explicit SetVelocityChurn(std::vector<EntityHandle> es)
        : entities_(std::move(es)) {}
    const char* name() const noexcept override { return "setVelocity"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Velocity};
    }
    void update(SystemContext& ctx) override {
        const auto t = ctx.tick();
        ctx.parallelFor(static_cast<std::uint32_t>(entities_.size()), 256,
            [this, t](Range r, CommandBuffer& cb) {
                for (auto i = r.begin; i < r.end; ++i) {
                    Velocity v{};
                    v.linear.x = static_cast<float>(((i + t) & 0xFF)) * 0.01f;
                    cb.setVelocity(entities_[i], v);
                }
            });
    }
private:
    std::vector<EntityHandle> entities_;
};

class AddTagChurn : public ISystem {
public:
    explicit AddTagChurn(std::vector<EntityHandle> es)
        : entities_(std::move(es)) {}
    const char* name() const noexcept override { return "addRemoveTag"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        const bool addPhase = (ctx.tick() & 1) == 0;
        ctx.parallelFor(static_cast<std::uint32_t>(entities_.size()), 256,
            [this, addPhase](Range r, CommandBuffer& cb) {
                for (auto i = r.begin; i < r.end; ++i) {
                    if (addPhase) cb.addTag(entities_[i], Component::StaticTag);
                    else          cb.removeTag(entities_[i], Component::StaticTag);
                }
            });
    }
private:
    std::vector<EntityHandle> entities_;
};

class SpawnDestroyChurn : public ISystem {
public:
    explicit SpawnDestroyChurn(std::uint32_t spawnPerTick)
        : spawnPerTick_(spawnPerTick) {}
    const char* name() const noexcept override { return "spawnDestroy"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        const bool spawnPhase = (ctx.tick() & 1) == 0;
        if (spawnPhase) {
            ctx.single([this](Range, CommandBuffer& cb) {
                for (std::uint32_t i = 0; i < spawnPerTick_; ++i) {
                    Transform tr{};
                    tr.position.x = static_cast<float>(i);
                    cb.spawn(tr);
                }
            });
        } else {
            const auto entSpan = ctx.world().entities();
            const std::size_t toKill =
                std::min<std::size_t>(spawnPerTick_, entSpan.size());
            std::vector<EntityHandle> kill(entSpan.end() - toKill,
                                           entSpan.end());
            ctx.single([kill = std::move(kill)](Range, CommandBuffer& cb) {
                for (auto e : kill) cb.destroy(e);
            });
        }
    }
private:
    std::uint32_t spawnPerTick_;
};

/// Multi-archetype shape from `commit_path_bench`. 4 archetype kinds ×
/// 25k entities each (Transform / +Velocity / +Velocity+Health /
/// +Velocity+Health+BoundingVolume).
struct MultiArchWorkload : IGame {
    std::uint32_t count = kChurnCount;
    void onSetup(Engine&, World&, CommandBuffer& cb) override {
        for (std::uint32_t i = 0; i < count; ++i) {
            Bundle b{};
            b.transform.position.x = static_cast<float>(i);
            b.initialMask = ComponentSet{Component::Transform};
            const std::uint32_t kind = i & 3u;
            if (kind >= 1u) {
                b.velocity.linear.x = 1.0f;
                b.initialMask = b.initialMask | ComponentSet{Component::Velocity};
            }
            if (kind >= 2u) {
                b.health = Health{50.0f, 50.0f};
                b.initialMask = b.initialMask | ComponentSet{Component::Health};
            }
            if (kind >= 3u) {
                b.boundingVolume = BoundingVolume{{-1,-1,-1},{1,1,1}};
                b.initialMask = b.initialMask |
                    ComponentSet{Component::BoundingVolume};
            }
            cb.spawnBundle(b);
        }
    }
};

/// RPG-mix per-tick driver. ~94% value-only across all live entities,
/// ~5% mask flip on a subset, ~1% spawn/destroy churn. Designed to
/// look like the rpg_demo command stream from the engine's POV —
/// many archetypes, mostly value-only, with sparse global churn.
class RpgMixChurn : public ISystem {
public:
    explicit RpgMixChurn(std::vector<EntityHandle> es)
        : entities_(std::move(es)) {}
    const char* name() const noexcept override { return "rpgMix"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        const auto t = ctx.tick();

        // (a) Value-only: setTransform on everyone.
        ctx.parallelFor(static_cast<std::uint32_t>(entities_.size()), 512,
            [this, t](Range r, CommandBuffer& cb) {
                for (auto i = r.begin; i < r.end; ++i) {
                    Transform tr{};
                    tr.position.x = static_cast<float>((i + t) & 0xFFFF);
                    cb.setTransform(entities_[i], tr);
                }
            });

        // (b) Mask flip: toggle StaticTag on every 32nd entity.
        const std::uint32_t flipCount =
            static_cast<std::uint32_t>(entities_.size()) / 32u;
        const bool addPhase = (t & 1u) == 0u;
        if (flipCount > 0) {
            ctx.parallelFor(flipCount, 256,
                [this, addPhase](Range r, CommandBuffer& cb) {
                    for (auto i = r.begin; i < r.end; ++i) {
                        const auto e = entities_[(i * 32u) % entities_.size()];
                        if (addPhase) cb.addTag(e, Component::StaticTag);
                        else          cb.removeTag(e, Component::StaticTag);
                    }
                });
        }

        // (c) Spawn / destroy churn: ~1k per tick (alternating).
        constexpr std::uint32_t kChurnPerTick = 1024;
        if ((t & 1u) == 0u) {
            ctx.single([](Range, CommandBuffer& cb) {
                for (std::uint32_t i = 0; i < kChurnPerTick; ++i) {
                    Transform tr{};
                    tr.position.x = static_cast<float>(i);
                    cb.spawn(tr);
                }
            });
        } else {
            const auto entSpan = ctx.world().entities();
            const std::size_t toKill =
                std::min<std::size_t>(kChurnPerTick, entSpan.size());
            std::vector<EntityHandle> kill(entSpan.end() - toKill,
                                           entSpan.end());
            ctx.single([kill = std::move(kill)](Range, CommandBuffer& cb) {
                for (auto e : kill) cb.destroy(e);
            });
        }
    }
private:
    std::vector<EntityHandle> entities_;
};

// -----------------------------------------------------------------------------
// Per-step measurement. Records breakdown + wall-clock per tick.

struct PerTickSample {
    CommitBreakdown bd;
    std::uint64_t   stepNs       = 0;
    /// `EngineStats::commitDurationSeconds * 1e9` — engine-side aggregate
    /// of commit wall-clock for THIS step. Populated regardless of path;
    /// lets us compare single-mode commit cost vs sharded-mode commit
    /// cost without instrumenting the serial path separately.
    std::uint64_t   commitNs     = 0;
};

struct PerWorkloadResult {
    std::string                   workload;
    std::string                   system;
    bool                          sharded = false;
    std::uint32_t                 workers = 0;
    std::vector<PerTickSample>    samples;
};

template <typename GameT, typename Factory>
PerWorkloadResult measureWorkload(const char* workloadName,
                                  const char* systemName,
                                  bool sharded,
                                  std::uint32_t workers,
                                  int warmup, int iters,
                                  std::uint32_t entityCapHint,
                                  GameT&& game,
                                  Factory&& factory) {
    Config cfg = benchConfig(workers, entityCapHint, sharded);
    // SHARDED_OPTIMISATION.md S6 — Env-var override for A/B benching
    // the migration-batch path. Set THREADMAXX_NO_BATCH=1 to force
    // every same-(srcArch, dstMask) run through the per-cmd path.
    if (const char* nob = std::getenv("THREADMAXX_NO_BATCH"); nob && nob[0] == '1') {
        cfg.batchMigrateThreshold = std::numeric_limits<std::uint32_t>::max();
    }
    // SHARDED_OPTIMISATION.md S8 — Env-var override for the record-time
    // per-chunk routing path. Set THREADMAXX_NO_ROUTING=1 to fall back
    // to the pre-S8 Pass A scan (legacy path inside sharded commit).
    if (const char* nor = std::getenv("THREADMAXX_NO_ROUTING"); nor && nor[0] == '1') {
        cfg.recordTimeRouting = false;
    }
    // SHARDED_OPTIMISATION.md S9 — Env-var override for the inline-
    // largest-bin Pass C lane. Set THREADMAXX_NO_INLINE_LARGEST=1 to
    // revert to the pre-S9 lane where every large bin goes to a
    // worker and the sim thread only handles small bins + waits.
    if (const char* noi = std::getenv("THREADMAXX_NO_INLINE_LARGEST");
        noi && noi[0] == '1') {
        cfg.inlineLargestBin = false;
    }
    Engine engine(cfg);
    if (!engine.initialize(game)) {
        std::fprintf(stderr, "  init failed for %s\n", workloadName);
        return {};
    }
    std::vector<EntityHandle> seeded;
    {
        const auto s = engine.world().entities();
        seeded.assign(s.begin(), s.end());
    }
    engine.registerSystem(factory(std::move(seeded)));

    PerWorkloadResult out;
    out.workload = workloadName;
    out.system   = systemName;
    out.sharded  = sharded;
    out.workers  = workers;
    out.samples.reserve(static_cast<std::size_t>(iters));

    for (int i = 0; i < warmup; ++i) engine.step();

    Stopwatch sw;
    for (int i = 0; i < iters; ++i) {
        sw.start();
        engine.step();
        const std::uint64_t stepNs = sw.elapsedNs();
        const std::uint64_t commitNs = static_cast<std::uint64_t>(
            engine.stats().commitDurationSeconds * 1e9);
        out.samples.push_back(PerTickSample{
            engine.lastCommitBreakdown(), stepNs, commitNs});
    }
    engine.shutdown();
    return out;
}

// -----------------------------------------------------------------------------
// Aggregation + reporting.

struct Aggregate {
    double meanStepNs   = 0.0;
    double meanCommitNs = 0.0;
    double meanNsPassA  = 0.0;
    double meanNsPassB  = 0.0;
    double meanNsPassC  = 0.0;
    double meanNsLatch  = 0.0;
    double meanNsTotal  = 0.0;
    double meanCmds     = 0.0;
    double meanMigrate  = 0.0;
    double meanBinned   = 0.0;
    double meanGlobal   = 0.0;
    double meanActiveBins = 0.0;
    /// SHARDED_OPTIMISATION.md S5 — bins that took the inline serial
    /// lane (small-bin fast path).
    double meanInlineBins = 0.0;
    std::uint64_t fallbackTicks = 0;
    std::uint64_t shardedTicks  = 0;
    std::uint64_t nTicks        = 0;
};

Aggregate aggregate(const PerWorkloadResult& r) {
    Aggregate a;
    if (r.samples.empty()) return a;
    long double sStep = 0, sCom = 0, sA = 0, sB = 0, sC = 0, sLatch = 0, sTot = 0;
    long double sCmds = 0, sMig = 0, sBin = 0, sGlob = 0, sBins = 0, sInline = 0;
    for (const auto& s : r.samples) {
        sStep   += s.stepNs;
        sCom    += s.commitNs;
        sA      += s.bd.nsPassA;
        sB      += s.bd.nsPassB;
        sC      += s.bd.nsPassC;
        sLatch  += s.bd.nsLatchWait;
        sTot    += s.bd.nsTotal;
        sCmds   += s.bd.totalCommands;
        sMig    += s.bd.migratingCount;
        sBin    += s.bd.binnedValueOnly;
        sGlob   += s.bd.globalLaneApplied;
        sBins   += s.bd.activeBins;
        sInline += s.bd.inlineBinCount;
        if (s.bd.fallbackCalls > 0) ++a.fallbackTicks;
        if (s.bd.shardedCalls  > 0) ++a.shardedTicks;
    }
    const auto n = static_cast<long double>(r.samples.size());
    a.meanStepNs     = static_cast<double>(sStep   / n);
    a.meanCommitNs   = static_cast<double>(sCom    / n);
    a.meanNsPassA    = static_cast<double>(sA      / n);
    a.meanNsPassB    = static_cast<double>(sB      / n);
    a.meanNsPassC    = static_cast<double>(sC      / n);
    a.meanNsLatch    = static_cast<double>(sLatch  / n);
    a.meanNsTotal    = static_cast<double>(sTot    / n);
    a.meanCmds       = static_cast<double>(sCmds   / n);
    a.meanMigrate    = static_cast<double>(sMig    / n);
    a.meanBinned     = static_cast<double>(sBin    / n);
    a.meanGlobal     = static_cast<double>(sGlob   / n);
    a.meanActiveBins = static_cast<double>(sBins   / n);
    a.meanInlineBins = static_cast<double>(sInline / n);
    a.nTicks         = r.samples.size();
    return a;
}

void writeJsonl(std::ofstream& out, const PerWorkloadResult& r) {
    if (!out.is_open()) return;
    for (std::size_t i = 0; i < r.samples.size(); ++i) {
        const auto& s  = r.samples[i];
        const auto& bd = s.bd;
        out << '{'
            << "\"workload\":\""  << r.workload << "\","
            << "\"system\":\""    << r.system   << "\","
            << "\"sharded\":"     << (r.sharded ? "true" : "false") << ','
            << "\"workers\":"     << r.workers  << ','
            << "\"tick\":"        << i          << ','
            << "\"step_ns\":"     << s.stepNs   << ','
            << "\"commit_ns\":"   << s.commitNs << ','
            << "\"total_commands\":"    << bd.totalCommands    << ','
            << "\"total_value_only\":"  << bd.totalValueOnly   << ','
            << "\"migrating\":"         << bd.migratingCount   << ','
            << "\"global_lane_applied\":"<< bd.globalLaneApplied << ','
            << "\"binned_value_only\":" << bd.binnedValueOnly  << ','
            << "\"chunk_count\":"       << bd.chunkCount       << ','
            << "\"active_bins\":"       << bd.activeBins       << ','
            << "\"inline_bin_count\":" << bd.inlineBinCount << ','
            << "\"ns_pass_a\":"   << bd.nsPassA   << ','
            << "\"ns_pass_b\":"   << bd.nsPassB   << ','
            << "\"ns_pass_c\":"   << bd.nsPassC   << ','
            << "\"ns_latch_wait\":" << bd.nsLatchWait << ','
            << "\"ns_total\":"    << bd.nsTotal    << ','
            << "\"sharded_calls\":"  << bd.shardedCalls  << ','
            << "\"fallback_calls\":" << bd.fallbackCalls
            << "}\n";
    }
}

void printSummaryHeader() {
    std::printf("%-32s %-8s %10s %10s %10s %10s %10s %10s %8s %7s %7s %6s %6s\n",
        "workload", "path",
        "step_us", "commit_us", "passA_us", "passB_us", "passC_us", "wait_us",
        "cmd/tk", "bin/tk", "inl/tk", "shTk", "fbTk");
    std::printf("%.*s\n", 150,
        "-----------------------------------------------------------------"
        "------------------------------------------------------------------------"
        "-----");
}

void printSummaryRow(const PerWorkloadResult& r) {
    const Aggregate a = aggregate(r);
    std::printf("%-32s %-8s %10.2f %10.2f %10.2f %10.2f %10.2f %10.2f %8.0f %7.1f %7.1f %6llu %6llu\n",
        r.workload.c_str(),
        r.sharded ? "sharded" : "single",
        a.meanStepNs / 1e3,
        a.meanCommitNs / 1e3,
        a.meanNsPassA / 1e3,
        a.meanNsPassB / 1e3,
        a.meanNsPassC / 1e3,
        a.meanNsLatch / 1e3,
        a.meanCmds,
        a.meanActiveBins,
        a.meanInlineBins,
        static_cast<unsigned long long>(a.shardedTicks),
        static_cast<unsigned long long>(a.fallbackTicks));
}

} // namespace

int main(int argc, char** argv) {
    const char* jsonlPath = (argc >= 2) ? argv[1] : nullptr;
    std::ofstream jsonl;
    if (jsonlPath) {
        jsonl.open(jsonlPath, std::ios::out | std::ios::trunc);
        if (!jsonl.is_open()) {
            std::fprintf(stderr, "failed to open %s for write\n", jsonlPath);
            return 1;
        }
    }

    constexpr int kWarmup = 32;
    constexpr int kIters  = 256;
    constexpr std::uint32_t kWorkers = 4;
    constexpr std::uint32_t kSpawnPerTick = 1024;

    printSummaryHeader();

    std::vector<PerWorkloadResult> all;
    all.reserve(14);

    for (bool sharded : {false, true}) {
        // setTransform/Churn
        all.push_back(measureWorkload(
            "setTransform/Churn", "setTransform", sharded, kWorkers,
            kWarmup, kIters, kChurnCount, ChurnWorkload{},
            [](std::vector<EntityHandle> e) {
                return std::make_unique<SetTransformChurn>(std::move(e));
            }));
        printSummaryRow(all.back());
        writeJsonl(jsonl, all.back());

        // setVelocity/Churn
        all.push_back(measureWorkload(
            "setVelocity/Churn", "setVelocity", sharded, kWorkers,
            kWarmup, kIters, kChurnCount, ChurnWorkload{},
            [](std::vector<EntityHandle> e) {
                return std::make_unique<SetVelocityChurn>(std::move(e));
            }));
        printSummaryRow(all.back());
        writeJsonl(jsonl, all.back());

        // addRemoveTag/Churn
        all.push_back(measureWorkload(
            "addRemoveTag/Churn", "addRemoveTag", sharded, kWorkers,
            kWarmup, kIters, kChurnCount, ChurnWorkload{},
            [](std::vector<EntityHandle> e) {
                return std::make_unique<AddTagChurn>(std::move(e));
            }));
        printSummaryRow(all.back());
        writeJsonl(jsonl, all.back());

        // spawnDestroy/Churn
        all.push_back(measureWorkload(
            "spawnDestroy/Churn", "spawnDestroy", sharded, kWorkers,
            kWarmup, kIters, kChurnCount, ChurnWorkload{},
            [kSpawnPerTick](std::vector<EntityHandle>) {
                return std::make_unique<SpawnDestroyChurn>(kSpawnPerTick);
            }));
        printSummaryRow(all.back());
        writeJsonl(jsonl, all.back());

        // setTransform/MultiArch
        all.push_back(measureWorkload(
            "setTransform/MultiArch", "setTransform", sharded, kWorkers,
            kWarmup, kIters, kChurnCount, MultiArchWorkload{},
            [](std::vector<EntityHandle> e) {
                return std::make_unique<SetTransformChurn>(std::move(e));
            }));
        printSummaryRow(all.back());
        writeJsonl(jsonl, all.back());

        // RPG-mix
        all.push_back(measureWorkload(
            "RPG-mix", "rpgMix", sharded, kWorkers,
            kWarmup, kIters, 110'000u, RpgMixWorkload{},
            [](std::vector<EntityHandle> e) {
                return std::make_unique<RpgMixChurn>(std::move(e));
            }));
        printSummaryRow(all.back());
        writeJsonl(jsonl, all.back());

        // SmallWorld
        all.push_back(measureWorkload(
            "SmallWorld", "setTransform", sharded, kWorkers,
            kWarmup, kIters, kSmallWorldCount, SmallWorldWorkload{},
            [](std::vector<EntityHandle> e) {
                return std::make_unique<SetTransformChurn>(std::move(e));
            }));
        printSummaryRow(all.back());
        writeJsonl(jsonl, all.back());

        // SHARDED_OPTIMISATION.md S5 — ManyTinyBins: 10 240 entities
        // spread across 32 archetypes (~320/bin). Every Pass C bin is
        // below the S5 `kMinBinForJob = 256` threshold, so the inline
        // serial lane fires every tick. The S5 gate reads this row.
        all.push_back(measureWorkload(
            "ManyTinyBins", "setTransform", sharded, kWorkers,
            kWarmup, kIters, kManyTinyBinsCount, ManyTinyBinsWorkload{},
            [](std::vector<EntityHandle> e) {
                return std::make_unique<SetTransformChurn>(std::move(e));
            }));
        printSummaryRow(all.back());
        writeJsonl(jsonl, all.back());
    }

    if (jsonlPath) {
        std::printf("\nwrote per-tick JSON Lines to %s (%zu rows)\n",
            jsonlPath, all.size() * static_cast<std::size_t>(kIters));
    }
    return 0;
}
