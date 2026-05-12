// Verifies that parallelFor (exposed indirectly via SystemContext) actually
// dispatches across workers, that waitIdle drains correctly, and that two
// batches in a row both complete.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>
#include <mutex>
#include <set>
#include <thread>

namespace {

class CounterSystem : public threadmaxx::ISystem {
public:
    std::atomic<int> total{0};
    std::set<std::thread::id> threadsSeen;  // protected by ctx.single
    std::mutex setMutex;

    const char* name() const noexcept override { return "counter"; }
    void update(threadmaxx::SystemContext& ctx) override {
        // 64 work items, fine-grained so we get several chunks per worker.
        ctx.parallelFor(64, /*grain*/ 1,
            [this](threadmaxx::Range r, threadmaxx::CommandBuffer&) {
                for (auto i = r.begin; i < r.end; ++i) {
                    total.fetch_add(1, std::memory_order_relaxed);
                }
                std::lock_guard<std::mutex> lk(setMutex);
                threadsSeen.insert(std::this_thread::get_id());
            });
    }
};

class EmptyGame : public threadmaxx::IGame {
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {}
};

} // namespace

int main() {
    threadmaxx::Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 4;
    threadmaxx::Engine engine(cfg);
    EmptyGame game;
    CHECK(engine.initialize(game));

    auto sys = std::make_unique<CounterSystem>();
    auto* sysPtr = sys.get();
    engine.registerSystem(std::move(sys));

    // First step: 64 increments expected.
    engine.step();
    CHECK_EQ(sysPtr->total.load(), 64);

    // Workers should have shared the load. With 4 workers and 64 chunks we
    // expect more than one thread id to appear.
    CHECK(sysPtr->threadsSeen.size() >= std::size_t{2});

    // Second step: counter doubles, no leaks from waitIdle.
    sysPtr->total.store(0);
    engine.step();
    CHECK_EQ(sysPtr->total.load(), 64);

    engine.shutdown();
    EXIT_WITH_RESULT();
}
