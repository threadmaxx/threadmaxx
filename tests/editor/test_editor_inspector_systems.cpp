/// @file test_editor_inspector_systems.cpp
/// @brief E2 — engine with N registered systems → listSystems() returns
/// N summaries with correct names, wave indices, and lastStepMs values.

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/inspect.hpp>

#include <threadmaxx/Components.hpp>
#include <threadmaxx/System.hpp>

namespace {

struct CountingSystem final : threadmaxx::ISystem {
    const char* name_;
    int updates{0};
    explicit CountingSystem(const char* n) : name_(n) {}
    const char* name() const noexcept override { return name_; }
    void update(threadmaxx::SystemContext&) override { ++updates; }
};

struct SystemSeedGame final : threadmaxx::IGame {
    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {
        engine.registerSystem(std::make_unique<CountingSystem>("alpha"));
        engine.registerSystem(std::make_unique<CountingSystem>("beta"));
        engine.registerSystem(std::make_unique<CountingSystem>("gamma"));
        engine.registerSystem(std::make_unique<CountingSystem>("delta"));
    }
};

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    SystemSeedGame game;
    CHECK(engine.initialize(game));
    engine.step();

    threadmaxx::editor::Inspector ins{engine};
    const auto rows = ins.listSystems();
    CHECK_EQ(rows.size(), 4u);

    const char* expected[] = {"alpha", "beta", "gamma", "delta"};
    for (std::size_t i = 0; i < rows.size(); ++i) {
        CHECK(rows[i].name == expected[i]);
        // All four use default reads/writes -> ComponentSet::all(), so
        // they serialize. Waves are 0..3.
        CHECK_EQ(rows[i].waveIndex, static_cast<std::uint32_t>(i));
        // lastStepMs is non-negative.
        CHECK(rows[i].lastStepMs >= 0.0f);
    }

    engine.shutdown();
    EXIT_WITH_RESULT();
}
