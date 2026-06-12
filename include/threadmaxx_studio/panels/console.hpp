#pragma once

/// @file panels/console.hpp
/// @brief `ConsolePanel` — thin `IStudioPanel` wrapper around
/// `editor::Console`. The studio never owns its own console
/// subsystem; this panel reads `console.history()` and emits the
/// lines through the backend.

#include "../panel.hpp"

#include <string_view>

namespace threadmaxx::editor {
class Console;
} // namespace threadmaxx::editor

namespace threadmaxx::studio {

class ConsolePanel : public IStudioPanel {
public:
    /// @brief Borrow the editor console. Caller owns lifetime; the
    /// console must outlive the panel.
    explicit ConsolePanel(editor::Console& console) noexcept
        : console_(&console) {}

    std::string_view id() const noexcept override {
        return "studio.console";
    }
    std::string_view title() const noexcept override { return "Console"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief The wrapped editor console (non-owning).
    editor::Console& console() noexcept { return *console_; }

private:
    editor::Console* console_;
};

} // namespace threadmaxx::studio
