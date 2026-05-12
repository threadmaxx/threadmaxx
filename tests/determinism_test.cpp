// Two engines, same seed inputs: commit order is determined by parallelFor
// chunk index, not by which worker won the race. Therefore the entity
// handles and per-entity UserData should agree across runs even with many
// threads contending.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <vector>

namespace {

// Spawns one entity per parallelFor chunk on tick 0, tagging it with the
// chunk index. Across runs the chunk-index -> handle mapping must agree.
class ParallelSpawnSystem : public threadmaxx::ISystem {
public:
    bool done = false;
    const char* name() const noexcept override { return "psp"; }
    void update(threadmaxx::SystemContext& ctx) override {
        if (done) return;
        done = true;
        // 32 chunks of size 1 — guarantees one job per item.
        ctx.parallelFor(32, /*grain*/ 1,
            [](threadmaxx::Range r, threadmaxx::CommandBuffer& cb) {
                threadmaxx::RenderTag tag;
                tag.meshId = static_cast<std::int32_t>(r.begin);
                cb.spawn({}, {}, tag, threadmaxx::UserData{r.begin});
            });
    }
};

class EmptyGame : public threadmaxx::IGame {
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {}
};

std::vector<std::uint64_t> runOnce() {
    threadmaxx::Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 4;
    threadmaxx::Engine engine(cfg);
    EmptyGame game;
    engine.initialize(game);
    engine.registerSystem(std::make_unique<ParallelSpawnSystem>());
    engine.step();

    std::vector<std::uint64_t> userData;
    for (const auto& u : engine.world().userData()) {
        userData.push_back(u.value);
    }
    engine.shutdown();
    return userData;
}

} // namespace

int main() {
    const auto a = runOnce();
    const auto b = runOnce();

    CHECK_EQ(a.size(), std::size_t{32});
    CHECK_EQ(b.size(), std::size_t{32});

    // Submission order is chunk-index order: row i of the dense array
    // should hold the spawn from chunk i, i.e. userData == i.
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK_EQ(a[i], static_cast<std::uint64_t>(i));
    }
    CHECK(a == b);

    EXIT_WITH_RESULT();
}
