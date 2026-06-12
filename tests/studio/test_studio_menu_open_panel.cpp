/// @file test_studio_menu_open_panel.cpp
/// @brief ST3 — File→New Panel registers a stub panel; View→Close
/// removes it. Pins the action-callback contract that ties chrome to
/// `PanelHost`.

#include "Check.hpp"
#include "studio/StudioTestFixture.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/menu_bar.hpp>
#include <threadmaxx_studio/studio.hpp>

#include <memory>

int main() {
    threadmaxx::studio::test::ScopedSession env{};
    threadmaxx::studio::StudioApp app{env.session()};

    threadmaxx::studio::MenuBar menu;
    threadmaxx::editor::HeadlessBackend backend;
    CHECK(backend.initialize());

    auto stub = std::make_unique<
        threadmaxx::studio::test::StubPanel>("stub-panel", "Stub");
    auto* stubPtr = stub.get();

    menu.addAction("File", "New Panel",
        [&app, &stub] {
            if (stub) {
                app.panelHost().registerPanel(stub.get());
            }
        });

    menu.addAction("View", "Close Stub",
        [&app] {
            app.panelHost().unregisterPanel("stub-panel");
        });

    CHECK_EQ(menu.actionCount(), 2u);

    // File → New Panel registers the stub.
    CHECK_EQ(app.panelHost().panelCount(), 0u);
    CHECK(menu.trigger("File", "New Panel"));
    CHECK_EQ(app.panelHost().panelCount(), 1u);
    CHECK(app.panelHost().findPanel("stub-panel") == stubPtr);

    // View → Close removes it.
    CHECK(menu.trigger("View", "Close Stub"));
    CHECK_EQ(app.panelHost().panelCount(), 0u);
    CHECK(app.panelHost().findPanel("stub-panel") == nullptr);

    // Unknown action returns false.
    CHECK(!menu.trigger("Help", "About"));

    // render() emits one drawText per action so chrome is visible.
    threadmaxx::studio::test::ScopedSession env2{};
    threadmaxx::studio::StudioApp app2{env2.session()};
    threadmaxx::studio::test::StubPanel dataSrcOwner{"sink"};
    (void)dataSrcOwner;
    threadmaxx::studio::test::ScopedSession env3{};
    threadmaxx::studio::StudioApp app3{env3.session()};
    backend.beginFrame();
    // Reuse `MinimalSource` shape from the canary; menu doesn't read
    // any source state, so we just need *something* implementing the
    // interface.
    struct NullSource : threadmaxx::studio::IStudioDataSource {
        threadmaxx::studio::AttachMode mode() const noexcept override {
            return threadmaxx::studio::AttachMode::Direct;
        }
    } src;
    menu.render(backend, src);
    backend.endFrame();
    CHECK(backend.capturedFrame().size() >= 4u); // begin + 2 texts + end

    backend.shutdown();
    EXIT_WITH_RESULT();
}
