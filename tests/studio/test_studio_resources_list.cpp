/// @file test_studio_resources_list.cpp
/// @brief ST14 — track resources via editor::Inspector; panel render
/// emits one row per tracked entry; AssetReloaded events fire
/// reloadEventCount.

#include "Check.hpp"
#include "studio/StudioTestFixture.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_editor/inspect.hpp>
#include <threadmaxx_studio/panels/resources.hpp>

#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/Resource.hpp>

namespace {

struct Mesh { int triangles{0}; };

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    threadmaxx::studio::test::ScopedSession env{};

    auto& reg = env.engine().resources();
    auto m1 = reg.addRefCounted<Mesh>(Mesh{10});
    auto m2 = reg.addRefCounted<Mesh>(Mesh{20});

    threadmaxx::editor::Inspector inspector{env.engine()};
    inspector.trackResource(m1.id(), "low.obj");
    inspector.trackResource(m2.id(), "high.obj");

    threadmaxx::studio::ResourcesPanel panel{env.engine(), inspector};

    threadmaxx::editor::HeadlessBackend backend;
    CHECK(backend.initialize());
    NullSource src;

    backend.beginFrame();
    panel.render(backend, src);
    backend.endFrame();
    CHECK_EQ(panel.lastRowCount(), 2u);

    std::size_t lowSeen = 0;
    std::size_t highSeen = 0;
    for (const auto& op : backend.capturedFrame().ops) {
        if (op.op != threadmaxx::editor::CapturedOp::Op::DrawText) continue;
        if (op.text.find("low.obj") != std::string::npos)  ++lowSeen;
        if (op.text.find("high.obj") != std::string::npos) ++highSeen;
    }
    CHECK_EQ(lowSeen, 1u);
    CHECK_EQ(highSeen, 1u);

    // Fire an AssetReloaded event manually; the panel subscription
    // bumps the counter on the next channel drain (a single step
    // drains).
    CHECK_EQ(panel.reloadEventCount(), 0u);

    threadmaxx::AssetReloaded ev{};
    ev.oldIndex = m1.id().index;
    ev.oldGeneration = m1.id().generation;
    ev.newIndex = m1.id().index;
    ev.newGeneration = m1.id().generation + 1;
    ev.type = std::type_index(typeid(Mesh));
    env.engine().events<threadmaxx::AssetReloaded>().emit(ev);
    env.engine().step();

    CHECK_EQ(panel.reloadEventCount(), 1u);

    // Re-render to show the footer line.
    backend.beginFrame();
    panel.render(backend, src);
    backend.endFrame();
    bool footerSeen = false;
    for (const auto& op : backend.capturedFrame().ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText &&
            op.text.find("reload events: 1") != std::string::npos) {
            footerSeen = true;
        }
    }
    CHECK(footerSeen);

    backend.shutdown();
    EXIT_WITH_RESULT();
}
