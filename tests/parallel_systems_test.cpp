// Two systems with disjoint declared write sets should run concurrently
// in the same wave; defaulting to ComponentSet::all() should force them
// strictly sequential. We detect overlap via a synchronization checkpoint:
// system A waits to *observe* system B start, with a generous deadline.
// If A and B share a wave, A succeeds; if not, A times out.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>
#include <chrono>
#include <thread>

namespace {

std::atomic<bool> gBStarted{false};
std::atomic<bool> gAObservedBStart{false};
std::atomic<int>  gARan{0};
std::atomic<int>  gBRan{0};

class SystemA : public threadmaxx::ISystem {
public:
    bool parallelMode = true;
    const char* name() const noexcept override { return "A"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return parallelMode ? threadmaxx::ComponentSet{threadmaxx::Component::Transform}
                            : threadmaxx::ComponentSet::all();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return parallelMode ? threadmaxx::ComponentSet{threadmaxx::Component::Transform}
                            : threadmaxx::ComponentSet::all();
    }
    void update(threadmaxx::SystemContext&) override {
        gARan.fetch_add(1, std::memory_order_relaxed);
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds(150);
        while (!gBStarted.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        if (gBStarted.load(std::memory_order_acquire)) {
            gAObservedBStart.store(true, std::memory_order_release);
        }
    }
};

class SystemB : public threadmaxx::ISystem {
public:
    bool parallelMode = true;
    const char* name() const noexcept override { return "B"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return parallelMode ? threadmaxx::ComponentSet{threadmaxx::Component::UserData}
                            : threadmaxx::ComponentSet::all();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return parallelMode ? threadmaxx::ComponentSet{threadmaxx::Component::UserData}
                            : threadmaxx::ComponentSet::all();
    }
    void update(threadmaxx::SystemContext&) override {
        gBRan.fetch_add(1, std::memory_order_relaxed);
        gBStarted.store(true, std::memory_order_release);
    }
};

template <bool ParallelMode>
class TwoSystemGame : public threadmaxx::IGame {
    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {
        auto a = std::make_unique<SystemA>(); a->parallelMode = ParallelMode;
        auto b = std::make_unique<SystemB>(); b->parallelMode = ParallelMode;
        engine.registerSystem(std::move(a));
        engine.registerSystem(std::move(b));
    }
};

void resetGlobals() {
    gBStarted.store(false);
    gAObservedBStart.store(false);
    gARan.store(0);
    gBRan.store(0);
}

} // namespace

int main() {
    // Case 1: disjoint write sets → same wave → A observes B starting.
    {
        resetGlobals();
        threadmaxx::Config cfg;
        cfg.sleepToPace = false;
        cfg.workerCount = 2;
        threadmaxx::Engine engine(cfg);
        TwoSystemGame<true> game;
        CHECK(engine.initialize(game));
        engine.step();
        CHECK_EQ(gARan.load(), 1);
        CHECK_EQ(gBRan.load(), 1);
        CHECK(gAObservedBStart.load());
        engine.shutdown();
    }

    // Case 2: default reads/writes (all) → distinct waves → strict serial.
    // A waits the full deadline before B is allowed to start, so A must
    // *not* observe B's start flag.
    {
        resetGlobals();
        threadmaxx::Config cfg;
        cfg.sleepToPace = false;
        cfg.workerCount = 2;
        threadmaxx::Engine engine(cfg);
        TwoSystemGame<false> game;
        CHECK(engine.initialize(game));
        engine.step();
        CHECK_EQ(gARan.load(), 1);
        CHECK_EQ(gBRan.load(), 1);
        CHECK(!gAObservedBStart.load());
        engine.shutdown();
    }

    EXIT_WITH_RESULT();
}
