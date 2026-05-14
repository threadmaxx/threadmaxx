// §3.1 events<T>().subscribe(fn): persistent subscription helper.
// Callbacks fire at tick boundary, once per emitted event, in
// subscription order. unsubscribe stops further deliveries.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <vector>

namespace {

struct Bang {
    std::int32_t payload;
};

class Emitter : public threadmaxx::ISystem {
public:
    Emitter(threadmaxx::Engine& e) : channel_(e.events<Bang>()) {}
    const char* name() const noexcept override { return "emitter"; }
    void update(threadmaxx::SystemContext& ctx) override {
        const auto t = static_cast<std::int32_t>(ctx.tick() + 1);
        channel_.emit(Bang{t});
        channel_.emit(Bang{t * 10});
    }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
private:
    threadmaxx::EventChannel<Bang>& channel_;
};

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
    Engine engine(cfg);

    struct G : IGame {
        void onSetup(Engine&, World&, CommandBuffer&) override {}
    } game;
    CHECK(engine.initialize(game));

    auto& ch = engine.events<Bang>();
    CHECK_EQ(ch.subscriberCount(), std::size_t{0});

    std::vector<std::int32_t> a;
    std::vector<std::int32_t> b;
    const auto idA = ch.subscribe([&](const Bang& bang) { a.push_back(bang.payload); });
    const auto idB = ch.subscribe([&](const Bang& bang) { b.push_back(bang.payload); });
    CHECK(idA != 0);
    CHECK(idB != 0);
    CHECK(idA != idB);
    CHECK_EQ(ch.subscriberCount(), std::size_t{2});

    engine.registerSystem(std::make_unique<Emitter>(engine));

    // Step 1: emitter emits Bang{1} and Bang{10}. Drain runs at the end
    // of the tick — callbacks fire for both events on both subscribers.
    engine.step();
    CHECK_EQ(a.size(), std::size_t{2});
    CHECK_EQ(a[0], std::int32_t{1});
    CHECK_EQ(a[1], std::int32_t{10});
    CHECK_EQ(b.size(), std::size_t{2});
    CHECK_EQ(b[0], std::int32_t{1});
    CHECK_EQ(b[1], std::int32_t{10});

    // Step 2: emits {2, 20}.
    engine.step();
    CHECK_EQ(a.size(), std::size_t{4});
    CHECK_EQ(a[2], std::int32_t{2});
    CHECK_EQ(a[3], std::int32_t{20});

    // Unsubscribe A; only B keeps receiving.
    CHECK(ch.unsubscribe(idA));
    CHECK_EQ(ch.subscriberCount(), std::size_t{1});

    engine.step();  // emits {3, 30}
    CHECK_EQ(a.size(), std::size_t{4});                  // unchanged
    CHECK_EQ(b.size(), std::size_t{6});
    CHECK_EQ(b[4], std::int32_t{3});
    CHECK_EQ(b[5], std::int32_t{30});

    // Double-unsubscribe is a no-op (returns false).
    CHECK(!ch.unsubscribe(idA));

    // After unsubscribing the last live subscriber, drainTick() still
    // exposes events on the next tick — subscribe and drainTick are
    // independent views.
    CHECK(ch.unsubscribe(idB));
    CHECK_EQ(ch.subscriberCount(), std::size_t{0});

    engine.step();  // emits {4, 40}; no callbacks fire
    CHECK_EQ(a.size(), std::size_t{4});
    CHECK_EQ(b.size(), std::size_t{6});

    // drainTick() on a fresh tick should see this tick's emits (the
    // previous step's emits, drained by the engine).
    auto seen = ch.drainTick();
    CHECK_EQ(seen.size(), std::size_t{2});
    CHECK_EQ(seen[0].payload, std::int32_t{4});
    CHECK_EQ(seen[1].payload, std::int32_t{40});

    engine.shutdown();
    EXIT_WITH_RESULT();
}
