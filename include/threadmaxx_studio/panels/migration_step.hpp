#pragma once

/// @file panels/migration_step.hpp
/// @brief ST36 — `MigrationStepPanel`: shows the per-step trace of a
/// `MigrationResult` (one row per applied step). Useful for spotting
/// which type / version pair was rewritten on which tick of the
/// migration walk.

#include "../panel.hpp"

#include <cstddef>
#include <string_view>

namespace threadmaxx::migration {
struct MigrationResult;
} // namespace threadmaxx::migration

namespace threadmaxx::studio {

class MigrationStepPanel : public IStudioPanel {
public:
    MigrationStepPanel() noexcept = default;

    /// @brief Bind the panel to a result. Pointer must outlive the
    /// panel; pass `nullptr` to detach.
    void setResult(const threadmaxx::migration::MigrationResult* result) noexcept;

    [[nodiscard]] const threadmaxx::migration::MigrationResult*
    result() const noexcept {
        return result_;
    }

    /// @brief Cursor position. Used by `stepForward` / `stepBackward`
    /// to walk through the applied steps for visualization.
    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }

    /// @brief Move forward through the applied-steps list. Returns
    /// the new cursor position (clamped to the size).
    std::size_t stepForward() noexcept;
    /// @brief Move backward. Returns the new cursor.
    std::size_t stepBackward() noexcept;
    void rewind() noexcept { cursor_ = 0; }

    /// @brief Total applied steps in the bound result; zero when
    /// unbound.
    [[nodiscard]] std::size_t stepCount() const noexcept;

    std::string_view id() const noexcept override {
        return "migration.steps";
    }
    std::string_view title() const noexcept override {
        return "Migration Steps";
    }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    [[nodiscard]] std::size_t rowCount() const noexcept { return lastRows_; }
    void setMaxRows(std::size_t n) noexcept { maxRows_ = n; }

private:
    const threadmaxx::migration::MigrationResult* result_{nullptr};
    std::size_t cursor_{0};
    std::size_t maxRows_{16};
    std::size_t lastRows_{0};
};

} // namespace threadmaxx::studio
