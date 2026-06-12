/// @file examples/editor_demo/main.cpp
/// @brief End-to-end editor demo. Headless — exercises every editor
/// subsystem (Inspector + CommandStack + HotReload + Telemetry +
/// Selection + PropertyEditor + Gizmo + WorldDiff + Layout +
/// Console) against a small running engine and exits 0 on clean
/// round-trip.

#include <threadmaxx_editor/threadmaxx_editor.hpp>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx_reflect/registry.hpp>

#include <cstdio>
#include <memory>
#include <sstream>
#include <string>

namespace {

struct DemoGame final : threadmaxx::IGame {
    threadmaxx::EntityHandle hero{};

    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        hero = engine.reserveEntityHandle();
        threadmaxx::Transform t{};
        t.position = {0.0f, 0.0f, 0.0f};
        seed.spawn(hero, t);
        threadmaxx::Health h{};
        h.current = 80.0f; h.max = 100.0f;
        seed.setHealth(hero, h);
    }
};

struct SetHealthCommand final : threadmaxx::editor::IEditCommand {
    threadmaxx::EntityHandle target;
    threadmaxx::Health from, to;
    SetHealthCommand(threadmaxx::EntityHandle e,
                     threadmaxx::Health f, threadmaxx::Health t)
        : target(e), from(f), to(t) {}
    std::string_view name() const noexcept override { return "SetHealth"; }
    void apply(threadmaxx::CommandBuffer& cb) override { cb.setHealth(target, to); }
    void undo (threadmaxx::CommandBuffer& cb) override { cb.setHealth(target, from); }
};

int report(const char* label, bool ok) {
    std::printf("[demo] %-28s %s\n", label, ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}

} // namespace

int main() {
    int failures = 0;

    threadmaxx::Engine engine{threadmaxx::Config{}};
    DemoGame game;
    // Backend must outlive the session: ~EditorSession invokes
    // backend->shutdown() if a backend is still installed, and
    // destructors fire in reverse declaration order. Declaring
    // `backend` before `session` keeps the pointer valid for that
    // teardown call. (Alternative: explicit session.setBackend(nullptr)
    // before returning from main.)
    threadmaxx::editor::HeadlessBackend backend;
    threadmaxx::editor::EditorSession session{engine};
    failures += report("backend attaches", session.setBackend(&backend));

    threadmaxx::editor::CommandStack stack{engine};
    failures += report("engine.initialize", engine.initialize(game));
    engine.step();

    // 1) Inspector listing.
    threadmaxx::editor::Inspector ins{engine};
    failures += report("inspector lists hero",
                       ins.listEntities().size() == 1);

    // 2) Selection.
    threadmaxx::editor::SelectionState sel{engine.world()};
    sel.select(game.hero);
    failures += report("selection holds hero",
                       sel.currentSelection().entity == game.hero);

    // 3) Property edit + undo through reflect.
    threadmaxx::reflect::TypeRegistry reg;
    threadmaxx::editor::PropertyEditor ed{engine, reg};
    ed.addBuiltinBindings();
    ed.setField(stack, game.hero, "Health", "current",
                threadmaxx::reflect::Value::make<float>(25.0f));
    engine.step();
    failures += report("Health.current = 25",
                       engine.world().tryGetHealth(game.hero)->current == 25.0f);
    stack.undo();
    engine.step();
    failures += report("undo Health.current = 80",
                       engine.world().tryGetHealth(game.hero)->current == 80.0f);

    // 4) Gizmo drag + command + undo.
    threadmaxx::editor::TranslateGizmo giz;
    giz.beginDrag(threadmaxx::editor::GizmoAxis::X);
    auto r = giz.updateDrag(3.0f);
    threadmaxx::Transform oldT = *engine.world().tryGetTransform(game.hero);
    threadmaxx::Transform newT = oldT;
    newT.position = newT.position + r->delta;
    stack.execute(threadmaxx::editor::TranslateGizmo::makeTranslateCommand(
        game.hero, oldT, newT));
    engine.step();
    failures += report("gizmo translate +3 along X",
                       engine.world().tryGetTransform(game.hero)->position.x == 3.0f);

    // 5) Telemetry overlay.
    threadmaxx::editor::TelemetryOverlay overlay{engine};
    overlay.render(backend);
    failures += report("overlay drew at least 2 ops",
                       backend.capturedFrame().size() >= 2);

    // 6) Hot reload (no-op loader; just exercise the API).
    struct NoopLoader final : threadmaxx::IResourceLoader {
        void update(threadmaxx::Engine&) override {}
    };
    engine.addResourceLoader(std::make_unique<NoopLoader>());
    struct Texture { int w{}; int h{}; };
    auto tex = engine.resources().addRefCounted<Texture>(Texture{256, 256});
    threadmaxx::editor::HotReloadController hr{engine};
    hr.trackResource(tex.id(), "hero.png");
    auto hrRes = hr.requestReload({"hero.png", false});
    failures += report("hot reload request accepted", hrRes.ok);

    // 7) Diff against a fresh snapshot.
    auto before = engine.world().snapshot();
    stack.execute(std::make_unique<SetHealthCommand>(
        game.hero, *engine.world().tryGetHealth(game.hero),
        threadmaxx::Health{50.0f, 100.0f}));
    engine.step();
    auto after = engine.world().snapshot();
    auto diff = threadmaxx::editor::WorldDiff::compute(before, after);
    bool sawHealthDiff = false;
    for (const auto& e : diff.entries) {
        for (const auto& c : e.componentChanges) {
            if (c == "Health") sawHealthDiff = true;
        }
    }
    failures += report("diff sees Health change", sawHealthDiff);

    // 8) Layout save/load.
    threadmaxx::editor::LayoutState layoutState;
    layoutState.selectedPanel = "Properties";
    layoutState.panels["Hierarchy"] = true;
    threadmaxx::editor::LayoutManager layout{layoutState};
    std::stringstream io;
    layout.save(io);
    threadmaxx::editor::LayoutManager loaded;
    failures += report("layout load succeeds", loaded.load(io));
    failures += report("layout selectedPanel survives",
                       loaded.state().selectedPanel == "Properties");

    // 9) Console exec.
    threadmaxx::editor::Console console;
    threadmaxx::EntityHandle hero = game.hero;
    console.registerCommand({
        "heal",
        [hero](std::span<const std::string>)
            -> std::unique_ptr<threadmaxx::editor::IEditCommand> {
            return std::make_unique<SetHealthCommand>(
                hero,
                threadmaxx::Health{50.0f, 100.0f},
                threadmaxx::Health{100.0f, 100.0f});
        },
    });
    auto execR = console.exec(stack, "heal");
    failures += report("console exec queued",
                       execR == threadmaxx::editor::EditResult::Deferred);
    engine.step();
    failures += report("Health.current = 100 after heal",
                       engine.world().tryGetHealth(game.hero)->current == 100.0f);

    engine.shutdown();
    std::printf("[demo] failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
