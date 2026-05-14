// §3.1 batch-6 prep: World::archetypeSignatures().
//
// Verifies:
//   - Empty world reports an empty signature list.
//   - Multiple entities with the same mask collapse into one row with
//     count = number of those entities.
//   - Distinct masks produce distinct rows.
//   - Output is sorted by mask.bits() ascending (deterministic order).
//   - Sum of all row counts == world.size() (no entity unaccounted for).
//   - addComponent / removeComponent shows up as a signature transition.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <algorithm>
#include <cstdint>

namespace {

class EmptyGame : public threadmaxx::IGame {
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {}
};

template <typename Body>
class RecorderSystem : public threadmaxx::ISystem {
public:
    explicit RecorderSystem(Body b) : body_(std::move(b)) {}
    const char* name() const noexcept override { return "recorder"; }
    void update(threadmaxx::SystemContext& ctx) override {
        if (done_) return;
        done_ = true;
        auto& b = body_;
        ctx.single([&b](threadmaxx::Range r, threadmaxx::CommandBuffer& cb) {
            b(r, cb);
        });
    }
private:
    Body body_;
    bool done_ = false;
};

template <typename Body>
auto recorder(Body b) {
    return std::make_unique<RecorderSystem<Body>>(std::move(b));
}

std::uint32_t totalCount(const std::vector<threadmaxx::ArchetypeSignature>& s) {
    std::uint32_t t = 0;
    for (const auto& row : s) t += row.count;
    return t;
}

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;
    Engine engine(cfg);
    EmptyGame g;
    CHECK(engine.initialize(g));

    const auto& w = engine.world();

    // 1) Empty world: no signatures.
    {
        const auto sigs = w.archetypeSignatures();
        CHECK(sigs.empty());
    }

    // 2) Spawn 5 of one shape, 3 of another. Default-mask spawn yields
    //    Transform+Velocity+UserData+Acceleration; the bundled shape
    //    adds RenderTag.
    engine.registerSystem(recorder(
        [](Range, CommandBuffer& cb) {
            for (int i = 0; i < 5; ++i) cb.spawn(Transform{});
            RenderTag tag; tag.meshId = 1;
            for (int i = 0; i < 3; ++i) cb.spawn(Transform{}, {}, tag);
        }));
    engine.step();

    {
        const auto sigs = w.archetypeSignatures();
        CHECK_EQ(sigs.size(), std::size_t{2});
        // Sorted ascending → the "without RenderTag" mask comes first
        // (it lacks the RenderTag bit).
        CHECK(sigs[0].mask.bits() < sigs[1].mask.bits());
        CHECK_EQ(sigs[0].count, std::uint32_t{5});
        CHECK_EQ(sigs[1].count, std::uint32_t{3});
        CHECK(!sigs[0].mask.has(Component::RenderTag));
        CHECK(sigs[1].mask.has(Component::RenderTag));
        CHECK_EQ(totalCount(sigs), static_cast<std::uint32_t>(w.size()));
    }

    // 3) addComponent shifts entities between archetypes. Pick the first
    //    entity (no RenderTag) and attach Health to it — it moves out
    //    of archetype 0 into a new one.
    const auto target = w.entities()[0];
    engine.registerSystem(recorder(
        [target](Range, CommandBuffer& cb) {
            cb.addComponent<Health>(target, Health{10, 10});
        }));
    engine.step();

    {
        const auto sigs = w.archetypeSignatures();
        // We expect 3 distinct archetypes now: 4 entities w/o
        // RenderTag/Health, 1 with Health, 3 with RenderTag.
        CHECK_EQ(sigs.size(), std::size_t{3});
        // Output is sorted by bits ascending.
        CHECK(std::is_sorted(sigs.begin(), sigs.end(),
            [](const ArchetypeSignature& a, const ArchetypeSignature& b) {
                return a.mask.bits() < b.mask.bits();
            }));
        CHECK_EQ(totalCount(sigs), static_cast<std::uint32_t>(w.size()));

        // Locate the row with Health.
        auto it = std::find_if(sigs.begin(), sigs.end(),
            [](const ArchetypeSignature& s) {
                return s.mask.has(Component::Health);
            });
        CHECK(it != sigs.end());
        CHECK_EQ(it->count, std::uint32_t{1});
    }

    // 4) removeComponent reverses the transition. After removing Health
    //    we're back to 2 archetypes.
    engine.registerSystem(recorder(
        [target](Range, CommandBuffer& cb) {
            cb.removeComponent<Health>(target);
        }));
    engine.step();

    {
        const auto sigs = w.archetypeSignatures();
        CHECK_EQ(sigs.size(), std::size_t{2});
        CHECK_EQ(totalCount(sigs), static_cast<std::uint32_t>(w.size()));
        for (const auto& row : sigs) {
            CHECK(!row.mask.has(Component::Health));
        }
    }

    engine.shutdown();
    EXIT_WITH_RESULT();
}
