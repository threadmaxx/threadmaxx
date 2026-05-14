// §3.2 batch 7: EventChannel<T>::subscribeScoped returns a RAII
// Subscription that auto-detaches on destruction. Move-only. Safe to
// outlive the channel (the Subscription holds a weak_ptr to the
// channel's subscriber list).

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <utility>
#include <vector>

namespace {

struct Bang { int payload; };

} // namespace

int main() {
    using namespace threadmaxx;

    // ---- single-channel RAII --------------------------------------------
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } game;
        CHECK(engine.initialize(game));
        auto& ch = engine.events<Bang>();

        std::vector<int> seen;
        {
            Subscription sub = ch.subscribeScoped(
                [&](const Bang& b) { seen.push_back(b.payload); });
            CHECK(sub.valid());
            CHECK_EQ(ch.subscriberCount(), std::size_t{1});

            ch.emit(Bang{1});
            engine.step();
            CHECK_EQ(seen.size(), std::size_t{1});
            CHECK_EQ(seen[0], 1);

            ch.emit(Bang{2});
            engine.step();
            CHECK_EQ(seen.size(), std::size_t{2});
        }
        // sub destroyed: count drops to zero, further emits don't fire.
        CHECK_EQ(ch.subscriberCount(), std::size_t{0});

        ch.emit(Bang{3});
        engine.step();
        CHECK_EQ(seen.size(), std::size_t{2});

        engine.shutdown();
    }

    // ---- move semantics: assignment detaches the previous target --------
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } game;
        CHECK(engine.initialize(game));
        auto& ch = engine.events<Bang>();

        int callsA = 0;
        int callsB = 0;
        Subscription a = ch.subscribeScoped([&](const Bang&) { ++callsA; });
        Subscription b = ch.subscribeScoped([&](const Bang&) { ++callsB; });
        CHECK_EQ(ch.subscriberCount(), std::size_t{2});

        // Move-construct: source becomes invalid, target takes ownership.
        Subscription a2 = std::move(a);
        CHECK(a2.valid());
        CHECK(!a.valid());
        CHECK_EQ(ch.subscriberCount(), std::size_t{2});

        // Move-assign over b: b's previous subscription is detached.
        b = std::move(a2);
        CHECK_EQ(ch.subscriberCount(), std::size_t{1});

        ch.emit(Bang{0});
        engine.step();
        // Only the a-callback survived (now owned by b).
        CHECK_EQ(callsA, 1);
        CHECK_EQ(callsB, 0);

        engine.shutdown();
    }

    // ---- reset() detaches eagerly ---------------------------------------
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } game;
        CHECK(engine.initialize(game));
        auto& ch = engine.events<Bang>();

        Subscription sub = ch.subscribeScoped([](const Bang&) {});
        CHECK(sub.valid());
        sub.reset();
        CHECK(!sub.valid());
        CHECK_EQ(ch.subscriberCount(), std::size_t{0});
        // Double-reset is a no-op.
        sub.reset();

        engine.shutdown();
    }

    // ---- Subscription outlives its channel -------------------------------
    // The channel lives on the engine; the Subscription is on the stack
    // here, so it survives engine destruction. ~Subscription must
    // safely no-op because its weak_ptr to the subscriber list expires.
    {
        Subscription orphaned;
        {
            Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
            Engine engine(cfg);
            struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } game;
            CHECK(engine.initialize(game));
            auto& ch = engine.events<Bang>();
            orphaned = ch.subscribeScoped([](const Bang&) {});
            CHECK(orphaned.valid());
            engine.shutdown();
        }
        // Engine and channel are gone. orphaned still thinks it's valid
        // until we check (weak_ptr only expires when the source's
        // shared_ptr count hits zero — and that's when the channel was
        // destroyed). The destructor that fires when we leave this scope
        // must not crash; the locked-pointer guard inside Subscription's
        // reset() handles this.
        CHECK(!orphaned.valid());
    }

    EXIT_WITH_RESULT();
}
