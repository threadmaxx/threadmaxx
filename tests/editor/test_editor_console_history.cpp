/// @file test_editor_console_history.cpp
/// @brief E10 — history walks recover previous commands newest-first.

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/commands.hpp>
#include <threadmaxx_editor/console.hpp>

namespace {

struct NoopCmd final : threadmaxx::editor::IEditCommand {
    std::string_view name() const noexcept override { return "noop"; }
    void apply(threadmaxx::CommandBuffer&) override {}
    void undo (threadmaxx::CommandBuffer&) override {}
};

} // namespace

int main() {
    threadmaxx::editor::test::ScopedEngine env;
    threadmaxx::editor::CommandStack stack{env.engine()};
    threadmaxx::editor::Console console;
    console.registerCommand({
        "noop",
        [](std::span<const std::string>)
            -> std::unique_ptr<threadmaxx::editor::IEditCommand> {
            return std::make_unique<NoopCmd>();
        },
    });

    (void)console.exec(stack, "noop alpha");
    (void)console.exec(stack, "noop beta");
    (void)console.exec(stack, "noop gamma");

    CHECK(console.historyAt(0) == "noop gamma");
    CHECK(console.historyAt(1) == "noop beta");
    CHECK(console.historyAt(2) == "noop alpha");
    CHECK(console.historyAt(3).empty());

    EXIT_WITH_RESULT();
}
