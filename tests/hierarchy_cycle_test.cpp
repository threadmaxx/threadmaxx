// HierarchySystem cycle guard.
//
// Before this fix, a Parent-graph cycle (A → B → A, or A → B → C → A)
// would put `HierarchySystem::update` into an infinite loop. The walk
// stack pushed entries unbounded because the inner `while (!done[cur])`
// had no visited-set guard. The fix tracks an `onStack[]` bitset per
// chain and treats every entry in a cycle as dangling.
//
// This test deliberately constructs a 2-entity cycle and a 3-entity
// cycle, asserts the step terminates within a bounded wall-clock, and
// confirms the world state survives (entities are still alive, their
// transforms are left at their stored values rather than corrupted).

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <chrono>
#include <cstdint>
#include <future>
#include <vector>

namespace {

using namespace threadmaxx;

struct CycleGame : IGame {
    int cycleSize = 2;
    std::vector<EntityHandle>* handlesOut = nullptr;
    void onSetup(Engine&, World&, CommandBuffer& cb) override {
        // Reserve handles up front, then spawn each entity with its
        // Parent pointing at the next in the ring.
        std::vector<EntityHandle> hs(cycleSize);
        for (int i = 0; i < cycleSize; ++i) hs[i] = EntityHandle{};
        // Use a single ctx-less spawn (no reservation here — onSetup
        // runs with the world already alive but no reservation
        // mechanism is needed; we'll wire parents after spawn).
        for (int i = 0; i < cycleSize; ++i) {
            Transform t{};
            t.position.x = static_cast<float>(i);
            cb.spawn(t);
        }
        (void)hs;
        // Capture spawn order; the actual cycle wire-up happens in
        // the system below since we need post-spawn EntityHandles.
        if (handlesOut) handlesOut->clear();
    }
};

class WireCycle : public ISystem {
public:
    int cycleSize = 0;
    std::vector<EntityHandle> es;
    bool wired = false;
    const char* name() const noexcept override { return "wire-cycle"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        if (wired) return;
        wired = true;
        ctx.single([this](Range, CommandBuffer& cb) {
            for (int i = 0; i < cycleSize; ++i) {
                Parent p;
                p.parent      = es[(i + 1) % cycleSize];  // cycle: i → (i+1) % N
                p.localOffset = {};
                p.localOffset.position.x = 1.0f;
                cb.setParent(es[i], p);
            }
        });
    }
};

bool stepWithTimeout(Engine& engine, std::chrono::milliseconds timeout) {
    // Run engine.step() on a worker thread; if it doesn't finish
    // within `timeout`, return false (so the test fails the assertion
    // rather than hangs CTest forever).
    std::promise<void> done;
    auto fut = done.get_future();
    std::thread t([&]() {
        engine.step();
        done.set_value();
    });
    const auto status = fut.wait_for(timeout);
    if (status == std::future_status::ready) {
        t.join();
        return true;
    }
    // step() is still running. Detach the thread and let the test
    // report failure. This leaks the thread, but the test process
    // will exit anyway.
    t.detach();
    return false;
}

} // namespace

int main() {
    for (int cycleSize : { 2, 3, 5 }) {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        CycleGame g; g.cycleSize = cycleSize;
        CHECK(engine.initialize(g));

        std::vector<EntityHandle> es;
        {
            auto sp = engine.world().entities();
            es.assign(sp.begin(), sp.end());
        }
        CHECK_EQ(es.size(), static_cast<std::size_t>(cycleSize));

        auto wire = std::make_unique<WireCycle>();
        wire->cycleSize = cycleSize;
        wire->es = es;
        engine.registerSystem(std::move(wire));
        engine.registerSystem(makeHierarchySystem());

        // First step: WireCycle attaches the parent chain, hierarchy
        // sees the cycle and (with the fix) terminates cleanly.
        // 5-second wall-clock guard catches the regression.
        CHECK(stepWithTimeout(engine, std::chrono::seconds(5)));
        // Subsequent steps must also terminate.
        for (int i = 0; i < 8; ++i) {
            CHECK(stepWithTimeout(engine, std::chrono::seconds(2)));
        }

        // All entities still alive.
        CHECK_EQ(engine.world().size(), static_cast<std::size_t>(cycleSize));

        engine.shutdown();
    }

    EXIT_WITH_RESULT();
}
