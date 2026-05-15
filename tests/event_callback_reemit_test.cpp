// Re-emit-from-callback ordering.
//
// `EventChannel<T>::subscribe(fn)`'s callbacks fire during the tick-
// boundary drain, BEFORE the front/back swap. After §3.6 batch 13c
// the back stack is captured at drain start via an atomic exchange,
// so a callback that emits MORE events onto the same channel publishes
// them onto the fresh (now-empty) back stack — they appear in the next
// tick's `drainTick`, not the current iteration. Test:
//
//   tick T: emit "A" → drains next tick
//   tick T+1: drain fires "A" → callback emits "B" onto back stack
//             drainTick during this tick sees "A"
//             drain (at tick boundary) captures "B"
//   tick T+2: drainTick sees "B"
//
// Before the lock-free emit, the mutex-protected emit held the back
// mutex during subscriber iteration — a callback that emitted would
// recursively lock and deadlock. The new design fixes that as a side
// benefit; this test pins the documented "re-emit lands next tick"
// contract.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>
#include <vector>

namespace {

using namespace threadmaxx;

struct Ping { int seq; };

class Reader : public ISystem {
public:
    Reader(Engine& e) : channel_(e.events<Ping>()) {}
    const char* name() const noexcept override { return "reader"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }
    void update(SystemContext&) override {
        for (const auto& p : channel_.drainTick()) {
            seen.push_back(p.seq);
        }
    }
    std::vector<int> seen;
private:
    EventChannel<Ping>& channel_;
};

class Emitter : public ISystem {
public:
    Emitter(Engine& e) : channel_(e.events<Ping>()) {}
    const char* name() const noexcept override { return "emitter"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }
    void update(SystemContext& ctx) override {
        if (ctx.tick() == 0) channel_.emit(Ping{1});
        // Only emit once; the subscriber's re-emit handles the rest.
    }
private:
    EventChannel<Ping>& channel_;
};

} // namespace

int main() {
    Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
    Engine engine(cfg);
    struct G : IGame {
        Reader* readerPtr = nullptr;
        void onSetup(Engine& eng, World&, CommandBuffer&) override {
            auto r = std::make_unique<Reader>(eng);
            readerPtr = r.get();
            eng.registerSystem(std::move(r));
            eng.registerSystem(std::make_unique<Emitter>(eng));
        }
    } g;
    CHECK(engine.initialize(g));

    auto& chan = engine.events<Ping>();
    std::vector<int> subscriberSaw;
    auto sub = chan.subscribeScoped([&chan, &subscriberSaw](const Ping& p) {
        subscriberSaw.push_back(p.seq);
        // Re-emit: publish a follow-up event with seq+10. With the
        // mutex-based emit this would deadlock. With the lock-free
        // MPSC emit, the new event lands on the now-empty back stack
        // (drain has already exchanged it to nullptr), so it appears
        // in the NEXT tick's drainTick.
        if (p.seq < 100) {
            chan.emit(Ping{p.seq + 10});
        }
    });

    // Tick 0: Emitter publishes Ping{1}. drainTick sees nothing
    // (Reader runs before the drain happens).
    engine.step();
    CHECK_EQ(g.readerPtr->seen.size(), std::size_t{0});

    // Tick 1: subscribers fire on Ping{1} → emit Ping{11}.
    // drainTick sees Ping{1}.
    engine.step();
    CHECK_EQ(g.readerPtr->seen.size(), std::size_t{1});
    CHECK_EQ(g.readerPtr->seen[0], 1);

    // Tick 2: subscribers fire on Ping{11} → emit Ping{21}.
    // drainTick sees Ping{11}.
    engine.step();
    CHECK_EQ(g.readerPtr->seen.size(), std::size_t{2});
    CHECK_EQ(g.readerPtr->seen[1], 11);

    // Tick 3..10: chain continues.
    for (int i = 0; i < 8; ++i) engine.step();

    // Subscriber saw 1, 11, 21, 31, ...
    CHECK(subscriberSaw.size() >= 10);
    for (std::size_t i = 0; i + 1 < subscriberSaw.size(); ++i) {
        CHECK_EQ(subscriberSaw[i + 1] - subscriberSaw[i], 10);
    }

    engine.shutdown();
    EXIT_WITH_RESULT();
}
