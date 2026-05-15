// §3.6 batch 13c: lock-free MPSC EventChannel correctness under
// heavy concurrent emit.
//
// EventChannel::emit now uses a Treiber-stack CAS prepend (no mutex);
// drain atomically detaches the entire stack and reverses it back to
// FIFO order. This test exercises:
//
//   (1) Many concurrent producers fan into a single channel and the
//       drained event count matches the total emitted (no loss, no
//       duplication).
//   (2) Per-producer emit order is preserved on drain. Each worker
//       emits (producerId, sequence) pairs in ascending sequence; the
//       drained span, filtered to one producerId, must show monotonic
//       sequences.
//   (3) `pendingCount` tracks emits within a tick and resets after
//       drain.
//   (4) Drain releases all node allocations (verified indirectly via
//       a large emit volume + tick churn — leak would surface under
//       a sanitizer build).

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

struct Stamp {
    std::uint32_t producer;
    std::uint32_t sequence;
};

class Collector : public threadmaxx::ISystem {
public:
    Collector(threadmaxx::Engine& e) : channel_(e.events<Stamp>()) {}
    const char* name() const noexcept override { return "collect"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
    void update(threadmaxx::SystemContext&) override {
        auto evs = channel_.drainTick();
        for (const auto& e : evs) {
            collected.push_back(e);
        }
    }
    std::vector<Stamp> collected;
private:
    threadmaxx::EventChannel<Stamp>& channel_;
};

} // namespace

int main() {
    using namespace threadmaxx;

    // ---- (1) Concurrent emit from 8 threads × 10k events each --------
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine e(cfg);
        struct G : IGame {
            Collector* cp = nullptr;
            void onSetup(Engine& eng, World&, CommandBuffer&) override {
                auto c = std::make_unique<Collector>(eng);
                cp = c.get();
                eng.registerSystem(std::move(c));
            }
        } g;
        CHECK(e.initialize(g));

        auto& chan = e.events<Stamp>();

        constexpr std::uint32_t kProducers   = 8;
        constexpr std::uint32_t kPerProducer = 10000;

        std::vector<std::thread> ts;
        ts.reserve(kProducers);
        std::atomic<bool> go{false};
        for (std::uint32_t p = 0; p < kProducers; ++p) {
            ts.emplace_back([&, p]() {
                while (!go.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                for (std::uint32_t s = 0; s < kPerProducer; ++s) {
                    chan.emit(Stamp{p, s});
                }
            });
        }
        go.store(true, std::memory_order_release);
        for (auto& t : ts) t.join();

        // pendingCount reflects emits without locking (relaxed atomic).
        CHECK_EQ(chan.pendingCount(),
                 static_cast<std::size_t>(kProducers * kPerProducer));

        // Two steps so the Collector's drainTick() sees the burst:
        // tick 1 swaps the back stack into front_; tick 2's Collector
        // reads what tick 1 made readable.
        e.step();
        e.step();

        // After drain, pendingCount has been reset.
        CHECK_EQ(chan.pendingCount(), std::size_t{0});

        CHECK_EQ(g.cp->collected.size(),
                 static_cast<std::size_t>(kProducers * kPerProducer));

        // Per-producer monotonic sequence check.
        std::vector<std::uint32_t> lastSeen(kProducers, 0);
        std::vector<bool>          everSeen(kProducers, false);
        for (const auto& s : g.cp->collected) {
            CHECK(s.producer < kProducers);
            if (everSeen[s.producer]) {
                // sequences from a single producer must arrive in
                // ascending order on drain (Treiber-stack reverse
                // restores per-thread FIFO).
                CHECK(s.sequence > lastSeen[s.producer]);
            }
            everSeen[s.producer]   = true;
            lastSeen[s.producer]   = s.sequence;
        }
        for (std::uint32_t p = 0; p < kProducers; ++p) {
            CHECK(everSeen[p]);
            CHECK_EQ(lastSeen[p], kPerProducer - 1);
        }

        e.shutdown();
    }

    // ---- (2) Drain reclaims all node allocations -------------------
    //
    // No direct assertion (allocator stats aren't portable), but a
    // sanitizer build would catch a leak across 100 churn iterations
    // of emit + step + drain.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine e(cfg);
        struct G : IGame {
            void onSetup(Engine&, World&, CommandBuffer&) override {}
        } g;
        CHECK(e.initialize(g));
        auto& chan = e.events<Stamp>();
        for (int iter = 0; iter < 100; ++iter) {
            for (std::uint32_t i = 0; i < 1000; ++i) {
                chan.emit(Stamp{0, i});
            }
            e.step();
        }
        e.shutdown();
    }

    EXIT_WITH_RESULT();
}
