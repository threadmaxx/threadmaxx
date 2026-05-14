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

#include <atomic>
#include <mutex>
#include <unordered_set>
#include <cstdint>

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
    EXIT_WITH_RESULT();
}
