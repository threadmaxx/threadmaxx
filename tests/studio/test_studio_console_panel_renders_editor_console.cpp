/// @file test_studio_console_panel_renders_editor_console.cpp
/// @brief ST3 — ConsolePanel wraps editor::Console: when the console
/// has an executed command in its history, the panel's render() emits
/// that line through the backend.

#include "Check.hpp"
#include "studio/StudioTestFixture.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_editor/commands.hpp>
#include <threadmaxx_editor/console.hpp>
#include <threadmaxx_studio/panels/console.hpp>

#include <memory>

namespace {

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    threadmaxx::studio::test::ScopedSession env{};

    threadmaxx::editor::CommandStack stack{env.engine()};
    threadmaxx::editor::Console console;
    // Register an echo command that produces no IEditCommand —
    // the history entry alone is what we assert on.
    console.registerCommand({
        "echo",
        [](std::span<const std::string>)
            -> std::unique_ptr<threadmaxx::editor::IEditCommand> {
            return nullptr;
        },
    });

    // Exec produces a history entry even if no IEditCommand was
    // returned (the editor's exec contract records the line either
    // way).
    (void)console.exec(stack, "echo hello-from-st3");

    threadmaxx::studio::ConsolePanel panel{console};
    CHECK(&panel.console() == &console);

    threadmaxx::editor::HeadlessBackend backend;
    CHECK(backend.initialize());
    NullSource src;

    backend.beginFrame();
    panel.render(backend, src);
    backend.endFrame();

    // Find the captured drawText whose text contains the line.
    bool foundLine = false;
    for (const auto& op : backend.capturedFrame().ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText &&
            op.text.find("echo hello-from-st3") != std::string::npos) {
            foundLine = true;
            break;
        }
    }
    CHECK(foundLine);

    backend.shutdown();
    EXIT_WITH_RESULT();
}
