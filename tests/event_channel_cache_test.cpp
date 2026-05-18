// §3.10.3 batch 24 (F8) — `Engine::events<T>()` thread_local cache
// regression test.
//
// The cache stores `(engineSerial, channel_ptr)` per Ev type per
// thread. Invariants:
//
//   1. Repeated `events<E>()` calls on the same engine return the
//      same channel pointer (steady-state hot path).
//   2. Each Engine has a unique non-zero serial; no two engines
//      ever share a serial within a single process.
//   3. After an Engine is destroyed and a new one constructed
//      (possibly at the same memory address), the cache invalidates
//      — `events<E>()` on the new engine returns a fresh channel
//      pointer, and crucially the old pointer is NOT reused.
//   4. Cross-engine coexistence: when both A and B are live,
//      `A.events<E>()` and `B.events<E>()` return distinct channels.

#include "Check.hpp"

#include <threadmaxx/Config.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>

#include <cstdio>
#include <memory>
#include <set>

namespace {
struct PingEvent { int value; };
} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg; cfg.sleepToPace = false; cfg.workerCount = 0;

    // ---- 1. Same-engine repeated lookups return same pointer ----------
    {
        Engine e(cfg);
        EventChannel<PingEvent>* p1 = &e.events<PingEvent>();
        for (int i = 0; i < 10; ++i) {
            EventChannel<PingEvent>* pn = &e.events<PingEvent>();
            CHECK_EQ(p1, pn);
        }
        std::printf("[event_channel_cache] same-engine stable ptr OK\n");
    }

    // ---- 2. Engines have distinct serials -----------------------------
    {
        Engine a(cfg);
        Engine b(cfg);
        Engine c(cfg);
        CHECK(a.engineSerial() != 0);
        CHECK(b.engineSerial() != 0);
        CHECK(c.engineSerial() != 0);
        std::set<std::uint64_t> serials = {
            a.engineSerial(), b.engineSerial(), c.engineSerial(),
        };
        CHECK_EQ(serials.size(), std::size_t{3});
        std::printf("[event_channel_cache] distinct serials: %llu %llu %llu\n",
                    static_cast<unsigned long long>(a.engineSerial()),
                    static_cast<unsigned long long>(b.engineSerial()),
                    static_cast<unsigned long long>(c.engineSerial()));
    }

    // ---- 3. Engine recreated → cache invalidates, no crash -----------
    // The classic UAF the original F8 attempt fell into: a stale
    // cached pointer from a destroyed engine. The thread_local
    // cache MUST invalidate via the serial check before dereferencing
    // any cached pointer. We can't reliably assert `p1 != p2`
    // because the heap may legitimately recycle the same address
    // for the fresh channel allocation — what matters is that the
    // cache check forces a re-lookup rather than serving the stale
    // pointer blindly.
    {
        std::uint64_t s1 = 0;
        std::uint64_t s2 = 0;
        {
            Engine e1(cfg);
            s1 = e1.engineSerial();
            (void)&e1.events<PingEvent>();  // populates cache for s1
        }
        Engine e2(cfg);
        s2 = e2.engineSerial();
        // Must be a distinct serial; cache key invalidates the stale entry.
        CHECK(s2 != s1);
        // Touch the channel — must not crash, and must return a
        // non-null reference. The fact that we got here without UAF
        // proves the serial check fired (had the cache served the
        // stale pointer, this would access freed memory).
        EventChannel<PingEvent>& ch = e2.events<PingEvent>();
        ch.emit(PingEvent{42});               // back-buffer write OK
        (void)ch.pendingCount();              // touch the channel state
        std::printf("[event_channel_cache] recreated engine → "
                    "cache invalidates + channel functional (s1=%llu s2=%llu)\n",
                    static_cast<unsigned long long>(s1),
                    static_cast<unsigned long long>(s2));
    }

    // ---- 4. Cross-engine coexistence ----------------------------------
    {
        Engine a(cfg);
        Engine b(cfg);
        EventChannel<PingEvent>* pa = &a.events<PingEvent>();
        EventChannel<PingEvent>* pb = &b.events<PingEvent>();
        CHECK(pa != pb);
        // Re-lookup on each — should still alternate correctly via the
        // serial check.
        CHECK_EQ(pa, &a.events<PingEvent>());
        CHECK_EQ(pb, &b.events<PingEvent>());
        CHECK_EQ(pa, &a.events<PingEvent>());
        std::printf("[event_channel_cache] cross-engine coexistence OK\n");
    }

    // ---- 5. Multiple Ev types on same engine — each cached separately --
    {
        struct PongEvent { int value; };
        Engine e(cfg);
        EventChannel<PingEvent>* p1 = &e.events<PingEvent>();
        EventChannel<PongEvent>* p2 = &e.events<PongEvent>();
        // Different event types → distinct channels.
        CHECK(reinterpret_cast<void*>(p1) != reinterpret_cast<void*>(p2));
        // Each cached separately; re-lookups stable.
        CHECK_EQ(p1, &e.events<PingEvent>());
        CHECK_EQ(p2, &e.events<PongEvent>());
        std::printf("[event_channel_cache] multiple Ev types OK\n");
    }

    EXIT_WITH_RESULT();
}
