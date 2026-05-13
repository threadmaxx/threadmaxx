// §3.3 EventChannel: emit/drainTick semantics, double-buffer flip, and
// cross-system visibility.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>

namespace {

struct Damage {
    std::uint64_t targetId;
    std::int32_t  amount;
};

// Emits one Damage event per tick with a stable, easy-to-trace amount.
class Emitter : public threadmaxx::ISystem {
public:
    Emitter(threadmaxx::Engine& e) : channel_(e.events<Damage>()) {}
    const char* name() const noexcept override { return "emitter"; }
    void update(threadmaxx::SystemContext& ctx) override {
        // tick() is the tick this step is computing; pre-increment relative
        // to engine.tick() returned after the step. Bias by +1 so the first
        // step emits amount=10, second emits 20, etc.
        const auto t = ctx.tick() + 1;
        channel_.emit(Damage{static_cast<std::uint64_t>(t),
                              static_cast<std::int32_t>(t * 10)});
    }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
private:
    threadmaxx::EventChannel<Damage>& channel_;
};

// Counts how many events it sees on each tick and accumulates their amounts.
class Reader : public threadmaxx::ISystem {
public:
    Reader(threadmaxx::Engine& e) : channel_(e.events<Damage>()) {}
    const char* name() const noexcept override { return "reader"; }
    void update(threadmaxx::SystemContext&) override {
        auto evs = channel_.drainTick();
        sawThisTick = evs.size();
        for (auto& e : evs) total += e.amount;
    }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }

    std::size_t  sawThisTick = 0;
    std::int64_t total       = 0;
private:
    threadmaxx::EventChannel<Damage>& channel_;
};

// Emits from a parallel worker chunk to verify thread-safe emit().
class ParallelEmitter : public threadmaxx::ISystem {
public:
    ParallelEmitter(threadmaxx::Engine& e) : channel_(e.events<Damage>()) {}
    const char* name() const noexcept override { return "pemit"; }
    void update(threadmaxx::SystemContext& ctx) override {
        auto& ch = channel_;
        ctx.parallelFor(32, /*grain*/ 4,
            [&ch](threadmaxx::Range r, threadmaxx::CommandBuffer&) {
                for (std::uint32_t i = r.begin; i < r.end; ++i) {
                    ch.emit(Damage{i, 1});
                }
            });
    }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
private:
    threadmaxx::EventChannel<Damage>& channel_;
};

} // namespace

int main() {
    using namespace threadmaxx;

    // Test 1: emit on tick T is visible to drainTick on tick T+1.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 2;
        Engine e(cfg);

        struct G : IGame {
            Reader*  readerPtr = nullptr;
            void onSetup(Engine& eng, World&, CommandBuffer&) override {
                // Reader runs first; emitter second. On the SAME tick,
                // reader.drainTick() should see PREVIOUS tick's events.
                auto reader = std::make_unique<Reader>(eng);
                readerPtr = reader.get();
                eng.registerSystem(std::move(reader));
                eng.registerSystem(std::make_unique<Emitter>(eng));
            }
        } g;
        CHECK(e.initialize(g));

        // Tick 1: emitter emits 1*10=10; reader sees nothing (no prior tick).
        e.step();
        CHECK_EQ(g.readerPtr->sawThisTick, std::size_t{0});

        // Tick 2: emitter emits 2*10=20; reader sees the previous tick's
        // single event with amount=10.
        e.step();
        CHECK_EQ(g.readerPtr->sawThisTick, std::size_t{1});
        CHECK_EQ(g.readerPtr->total,       std::int64_t{10});

        // Tick 3: reader picks up tick-2's event (amount=20).
        e.step();
        CHECK_EQ(g.readerPtr->sawThisTick, std::size_t{1});
        CHECK_EQ(g.readerPtr->total,       std::int64_t{10 + 20});

        e.shutdown();
    }

    // Test 2: thread-safe emit from worker jobs. 32 events emitted in
    // parallel across 8 chunks; the next tick's drain sees all 32.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 4;
        Engine e(cfg);
        struct G : IGame {
            Reader* rp = nullptr;
            void onSetup(Engine& eng, World&, CommandBuffer&) override {
                auto r = std::make_unique<Reader>(eng);
                rp = r.get();
                eng.registerSystem(std::move(r));
                eng.registerSystem(std::make_unique<ParallelEmitter>(eng));
            }
        } g;
        CHECK(e.initialize(g));
        e.step();
        CHECK_EQ(g.rp->sawThisTick, std::size_t{0});
        e.step();
        CHECK_EQ(g.rp->sawThisTick, std::size_t{32});
        CHECK_EQ(g.rp->total,       std::int64_t{32});
        e.shutdown();
    }

    EXIT_WITH_RESULT();
}
