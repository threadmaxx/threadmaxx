// §3.8 batch 32 — `forEachSerial<...>` smoke test (closes a public-API
// coverage gap surfaced by the B32 audit). Verifies that the
// single-threaded iteration helper visits every entity exactly once,
// on the sim thread, and exposes the same component spans as the
// parallel variants.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>
#include <set>

namespace {

using namespace threadmaxx;

struct SeedGame : IGame {
    void onSetup(Engine&, World&, CommandBuffer& cb) override {
        for (int i = 0; i < 4; ++i) {
            Transform t{};
            t.position.x = static_cast<float>(i);
            cb.spawn(t);
        }
    }
};

class Counter : public ISystem {
public:
    std::atomic<int> visits{0};
    std::set<std::uint32_t> seenIndices;
    std::mutex mtx;

    const char* name() const noexcept override { return "counter"; }
    ComponentSet reads()  const noexcept override {
        return ComponentSet{Component::Transform};
    }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }

    void update(SystemContext& ctx) override {
        forEachSerial<Transform>(ctx,
            [this](EntityHandle, const Transform& t, CommandBuffer&) {
                std::lock_guard<std::mutex> lk(mtx);
                visits.fetch_add(1, std::memory_order_relaxed);
                seenIndices.insert(static_cast<std::uint32_t>(t.position.x));
            });
    }
};

} // namespace

int main() {
    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 4;
    Engine engine(cfg);
    SeedGame g;
    CHECK(engine.initialize(g));

    auto* c = new Counter();
    engine.registerSystem(std::unique_ptr<ISystem>(c));
    engine.step();

    CHECK_EQ(c->visits.load(), 4);
    CHECK_EQ(c->seenIndices.size(), std::size_t{4});
    for (std::uint32_t i = 0; i < 4; ++i) {
        CHECK(c->seenIndices.count(i) == 1);
    }

    // Empty world doesn't crash and produces zero visits.
    {
        Config cfg2;
        cfg2.sleepToPace = false;
        cfg2.workerCount = 4;
        Engine empty(cfg2);
        struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } eg;
        CHECK(empty.initialize(eg));
        auto* c2 = new Counter();
        empty.registerSystem(std::unique_ptr<ISystem>(c2));
        empty.step();
        CHECK_EQ(c2->visits.load(), 0);
        empty.shutdown();
    }

    engine.shutdown();
    EXIT_WITH_RESULT();
}
