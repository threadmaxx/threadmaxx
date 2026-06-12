/// @file test_studio_task_graph_layout.cpp
/// @brief ST12 — TaskGraphPanel reads Engine::taskGraphSnapshot,
/// emits one row per node, exposes nodeCount / maxWave for tests.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/task_graph.hpp>

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/System.hpp>

#include <memory>

namespace {

class NoopSystem final : public threadmaxx::ISystem {
public:
    explicit NoopSystem(const char* n) : name_(n) {}
    const char* name() const noexcept override { return name_; }
    void update(threadmaxx::SystemContext&) override {}
private:
    const char* name_;
};

struct NoopGame final : threadmaxx::IGame {
    void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {}
};

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    threadmaxx::Engine engine{threadmaxx::Config{}};
    NoopGame game;
    engine.registerSystem(std::make_unique<NoopSystem>("Alpha"));
    engine.registerSystem(std::make_unique<NoopSystem>("Beta"));
    engine.registerSystem(std::make_unique<NoopSystem>("Gamma"));
    CHECK(engine.initialize(game));

    threadmaxx::studio::TaskGraphPanel panel{engine};

    threadmaxx::editor::HeadlessBackend backend;
    CHECK(backend.initialize());
    NullSource src;

    backend.beginFrame();
    panel.render(backend, src);
    backend.endFrame();

    CHECK_EQ(panel.lastNodeCount(), 3u);

    // 3 NoopSystems with default reads/writes = ComponentSet::all()
    // → strict registration-order chain → waves 0, 1, 2 → max wave 2.
    CHECK_EQ(panel.lastMaxWave(), 2u);

    std::size_t textOps = 0;
    bool sawAlpha = false;
    bool sawBeta = false;
    bool sawGamma = false;
    for (const auto& op : backend.capturedFrame().ops) {
        if (op.op != threadmaxx::editor::CapturedOp::Op::DrawText) continue;
        ++textOps;
        if (op.text.find("Alpha") != std::string::npos) sawAlpha = true;
        if (op.text.find("Beta") != std::string::npos)  sawBeta = true;
        if (op.text.find("Gamma") != std::string::npos) sawGamma = true;
    }
    CHECK_EQ(textOps, 3u);
    CHECK(sawAlpha);
    CHECK(sawBeta);
    CHECK(sawGamma);

    engine.shutdown();
    backend.shutdown();
    EXIT_WITH_RESULT();
}
