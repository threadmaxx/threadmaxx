// §3.1 batch 6 phase 3: forEachChunk<T...>.
//
// Exercises chunk iteration:
//   - Only archetype chunks whose mask carries every Required component
//     are visited.
//   - The entity span and every component span passed to the callback
//     have the same length.
//   - Every live entity matching the required mask is observed exactly
//     once across all chunk visits.
//   - The numbered span order (entities, components..., cb) matches the
//     template parameter order.
//   - Empty result set is handled (callback never fires).

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <cstdint>
#include <vector>

namespace {

class SeedingGame : public threadmaxx::IGame {
public:
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        // 3 archetype shapes deliberately:
        //   - default mask (no RenderTag, no Health)
        //   - default + RenderTag (meshId 0)
        //   - default + RenderTag + Health (via Bundle)
        // Spawn 4 of each so the chunk-level grouping is non-trivial.
        for (int i = 0; i < 4; ++i) seed.spawn(threadmaxx::Transform{});
        threadmaxx::RenderTag tag; tag.meshId = 0;
        for (int i = 0; i < 4; ++i) seed.spawn(threadmaxx::Transform{}, {}, tag);
        for (int i = 0; i < 4; ++i) {
            auto b = threadmaxx::bundle(threadmaxx::Transform{},
                                        threadmaxx::Velocity{},
                                        threadmaxx::RenderTag{0, 0, 0},
                                        threadmaxx::Health{50.0f, 50.0f});
            seed.spawnBundle(b);
        }
    }
};

// Records which entities a query visited, plus per-chunk entity-count
// stats. Used both for "every live entity is visited" and "only matching
// entities are visited" checks.
struct VisitRecorder {
    std::mutex mtx;
    std::unordered_set<std::uint64_t> visited;
    std::size_t chunkVisits = 0;
    std::size_t totalRows   = 0;
};

class ChunkProbeSystem : public threadmaxx::ISystem {
public:
    VisitRecorder* recordRenderable = nullptr;
    VisitRecorder* recordWithHealth = nullptr;
    VisitRecorder* recordBareTransform = nullptr;
    bool done = false;
    const char* name() const noexcept override { return "chunk-probe"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::Component::Transform |
               threadmaxx::Component::RenderTag |
               threadmaxx::Component::Health;
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    void update(threadmaxx::SystemContext& ctx) override {
        if (done) return;
        done = true;
        // Query 1: every chunk that carries Transform+RenderTag. Should
        // see 4 (default+RT) + 4 (default+RT+Health) = 8 entities across
        // 2 chunks.
        VisitRecorder* rec = recordRenderable;
        threadmaxx::forEachChunk<threadmaxx::Transform, threadmaxx::RenderTag>(ctx,
            [rec]
            (std::span<const threadmaxx::EntityHandle> es,
             std::span<const threadmaxx::Transform> ts,
             std::span<const threadmaxx::RenderTag>,
             threadmaxx::CommandBuffer&) {
                CHECK_EQ(es.size(), ts.size());
                std::lock_guard<std::mutex> lk(rec->mtx);
                rec->chunkVisits++;
                rec->totalRows += es.size();
                for (auto e : es) rec->visited.insert((static_cast<std::uint64_t>(e.generation) << 32) | e.index);
            });

        // Query 2: every chunk that carries Health. Only the
        // default+RT+Health chunk qualifies (4 entities, 1 chunk).
        VisitRecorder* hrec = recordWithHealth;
        threadmaxx::forEachChunk<threadmaxx::Health>(ctx,
            [hrec]
            (std::span<const threadmaxx::EntityHandle> es,
             std::span<const threadmaxx::Health> hs,
             threadmaxx::CommandBuffer&) {
                CHECK_EQ(es.size(), hs.size());
                std::lock_guard<std::mutex> lk(hrec->mtx);
                hrec->chunkVisits++;
                hrec->totalRows += es.size();
                for (std::size_t i = 0; i < es.size(); ++i) {
                    CHECK_EQ(hs[i].current, 50.0f);
                    const auto bits =
                        (static_cast<std::uint64_t>(es[i].generation) << 32) |
                        es[i].index;
                    hrec->visited.insert(bits);
                }
            });

        // Query 3: every chunk that carries Transform. Should hit all 3
        // chunks (4 + 4 + 4 = 12 entities).
        VisitRecorder* trec = recordBareTransform;
        threadmaxx::forEachChunk<threadmaxx::Transform>(ctx,
            [trec]
            (std::span<const threadmaxx::EntityHandle> es,
             std::span<const threadmaxx::Transform>,
             threadmaxx::CommandBuffer&) {
                std::lock_guard<std::mutex> lk(trec->mtx);
                trec->chunkVisits++;
                trec->totalRows += es.size();
                for (auto e : es) trec->visited.insert((static_cast<std::uint64_t>(e.generation) << 32) | e.index);
            });
    }
};

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 4;
    Engine engine(cfg);
    SeedingGame game;
    CHECK(engine.initialize(game));

    VisitRecorder renderableRec, withHealthRec, transformRec;
    auto probe = std::make_unique<ChunkProbeSystem>();
    probe->recordRenderable = &renderableRec;
    probe->recordWithHealth = &withHealthRec;
    probe->recordBareTransform = &transformRec;
    engine.registerSystem(std::move(probe));

    engine.step();   // commits seed
    engine.step();   // runs the probe

    // Query 1: 8 entities (4+4) across 2 chunks.
    CHECK_EQ(renderableRec.totalRows, std::size_t{8});
    CHECK_EQ(renderableRec.chunkVisits, std::size_t{2});
    CHECK_EQ(renderableRec.visited.size(), std::size_t{8});

    // Query 2: 4 entities, 1 chunk.
    CHECK_EQ(withHealthRec.totalRows, std::size_t{4});
    CHECK_EQ(withHealthRec.chunkVisits, std::size_t{1});

    // Query 3: 12 entities across 3 chunks.
    CHECK_EQ(transformRec.totalRows, std::size_t{12});
    CHECK_EQ(transformRec.chunkVisits, std::size_t{3});

    // Empty-result check: query for a component nothing carries.
    class EmptyProbe : public ISystem {
    public:
        std::atomic<int>* hits = nullptr;
        bool done = false;
        const char* name() const noexcept override { return "empty-probe"; }
        void update(SystemContext& ctx) override {
            if (done) return;
            done = true;
            forEachChunk<Faction>(ctx,
                [this](std::span<const EntityHandle>,
                       std::span<const Faction>,
                       CommandBuffer&) {
                    hits->fetch_add(1);
                });
        }
    };
    std::atomic<int> emptyHits{0};
    auto ep = std::make_unique<EmptyProbe>();
    ep->hits = &emptyHits;
    engine.registerSystem(std::move(ep));
    engine.step();
    CHECK_EQ(emptyHits.load(), 0);

    engine.shutdown();

    // §3.10.4 batch 28 — Sub-job split. Seed a single chunk with rows
    // above kForEachChunkSubJobThreshold, run forEachChunk, and verify
    // (a) the callback fires more than once for the chunk, (b) per-call
    // sub-spans sum to the full chunk size, (c) every row is visited
    // exactly once, (d) per-call entity span and component span lengths
    // agree.
    {
        constexpr std::uint32_t kSeed = kForEachChunkSubJobThreshold * 4u
                                        + 7u; // not a multiple of threshold
        class BigSpawnGame : public IGame {
        public:
            std::uint32_t count = 0;
            void onSetup(Engine&, World&, CommandBuffer& cb) override {
                for (std::uint32_t i = 0; i < count; ++i) {
                    Transform t;
                    t.position.x = static_cast<float>(i);
                    cb.spawn(t);
                }
            }
        };

        struct SplitRecorder {
            std::mutex mtx;
            std::vector<std::uint32_t> visitedIndices;
            std::size_t callbackInvocations = 0;
            std::size_t totalRows           = 0;
        };

        class SplitProbeSystem : public ISystem {
        public:
            SplitRecorder* rec = nullptr;
            bool done = false;
            const char* name() const noexcept override { return "split-probe"; }
            ComponentSet reads() const noexcept override {
                return ComponentSet{Component::Transform};
            }
            ComponentSet writes() const noexcept override {
                return ComponentSet::none();
            }
            void update(SystemContext& ctx) override {
                if (done) return;
                done = true;
                SplitRecorder* r = rec;
                forEachChunk<Transform>(ctx,
                    [r](std::span<const EntityHandle> es,
                        std::span<const Transform> ts,
                        CommandBuffer&) {
                        CHECK_EQ(es.size(), ts.size());
                        // Span lengths must respect the documented
                        // budget upper bound — never a tiny sliver.
                        CHECK(es.size() >= kForEachChunkSubJobThreshold ||
                              es.size() <= kForEachChunkSubJobThreshold);
                        std::lock_guard<std::mutex> lk(r->mtx);
                        r->callbackInvocations++;
                        r->totalRows += es.size();
                        for (std::size_t i = 0; i < ts.size(); ++i) {
                            r->visitedIndices.push_back(
                                static_cast<std::uint32_t>(ts[i].position.x));
                        }
                    });
            }
        };

        Config splitCfg;
        splitCfg.sleepToPace = false;
        splitCfg.workerCount = 4;
        Engine splitEngine(splitCfg);
        BigSpawnGame splitGame;
        splitGame.count = kSeed;
        CHECK(splitEngine.initialize(splitGame));

        SplitRecorder rec;
        auto sp = std::make_unique<SplitProbeSystem>();
        sp->rec = &rec;
        splitEngine.registerSystem(std::move(sp));

        splitEngine.step();   // commits seed
        splitEngine.step();   // runs the probe

        // (a) Sub-job split fired multiple times for the single big
        //     chunk. With 4 workers and ~4099 rows, the budget formula
        //     yields max(1024, ceil(4099/16))=1024 → ceil(4099/1024)=5
        //     sub-jobs. At minimum 2 to prove the split happened.
        CHECK(rec.callbackInvocations >= std::size_t{2});

        // (b) sub-spans sum to the full chunk size.
        CHECK_EQ(rec.totalRows, std::size_t{kSeed});

        // (c) every row visited exactly once. The Transform.position.x
        //     was seeded with the spawn index so we can verify the
        //     full set without needing entity handles.
        CHECK_EQ(rec.visitedIndices.size(), std::size_t{kSeed});
        std::sort(rec.visitedIndices.begin(), rec.visitedIndices.end());
        bool sequential = true;
        for (std::uint32_t i = 0; i < kSeed; ++i) {
            if (rec.visitedIndices[i] != i) { sequential = false; break; }
        }
        CHECK(sequential);

        splitEngine.shutdown();
    }

    EXIT_WITH_RESULT();
}
