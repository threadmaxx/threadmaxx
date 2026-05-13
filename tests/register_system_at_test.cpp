// §3.6a registerSystemAt: insert a system at a specific registration
// index. Verifies:
//   - clamping (position > count) appends like registerSystem
//   - middle insertion shifts later systems by one
//   - systemStats() ordering matches insertion order
//   - returned index is the actual insertion position

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <string>

namespace {

// Bare-bones system that records its onRegister + update name. We use
// reads/writes = none so all systems pack into one wave (irrelevant to
// the test, but keeps the schedule simple).
class NamedSystem : public threadmaxx::ISystem {
public:
    explicit NamedSystem(const char* n) : name_(n) {}
    const char* name() const noexcept override { return name_; }
    void update(threadmaxx::SystemContext&) override {}
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
private:
    const char* name_;
};

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 1;
    Engine engine(cfg);

    struct Game : IGame {
        void onSetup(Engine&, World&, CommandBuffer&) override {}
    } game;
    CHECK(engine.initialize(game));

    CHECK_EQ(engine.registeredSystemCount(), std::size_t{0});

    // Append A, B, C using registerSystem.
    engine.registerSystem(std::make_unique<NamedSystem>("A"));
    engine.registerSystem(std::make_unique<NamedSystem>("B"));
    engine.registerSystem(std::make_unique<NamedSystem>("C"));
    CHECK_EQ(engine.registeredSystemCount(), std::size_t{3});

    // Insert X at index 1: order should become A, X, B, C.
    const auto idx = engine.registerSystemAt(1, std::make_unique<NamedSystem>("X"));
    CHECK_EQ(idx, std::size_t{1});
    CHECK_EQ(engine.registeredSystemCount(), std::size_t{4});

    // Step so systemStats_ is in steady state.
    engine.step();
    auto stats = engine.systemStats();
    CHECK_EQ(stats.size(), std::size_t{4});
    CHECK(std::string(stats[0].name) == "A");
    CHECK(std::string(stats[1].name) == "X");
    CHECK(std::string(stats[2].name) == "B");
    CHECK(std::string(stats[3].name) == "C");

    // Insert at position past end: clamps to end and acts like registerSystem.
    const auto idxEnd = engine.registerSystemAt(999,
        std::make_unique<NamedSystem>("Z"));
    CHECK_EQ(idxEnd, std::size_t{4});
    CHECK_EQ(engine.registeredSystemCount(), std::size_t{5});
    engine.step();
    stats = engine.systemStats();
    CHECK(std::string(stats[4].name) == "Z");

    // Insert at front.
    const auto idxFront = engine.registerSystemAt(0,
        std::make_unique<NamedSystem>("FRONT"));
    CHECK_EQ(idxFront, std::size_t{0});
    engine.step();
    stats = engine.systemStats();
    CHECK_EQ(stats.size(), std::size_t{6});
    CHECK(std::string(stats[0].name) == "FRONT");
    CHECK(std::string(stats[1].name) == "A");

    engine.shutdown();
    EXIT_WITH_RESULT();
}
