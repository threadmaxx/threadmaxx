#pragma once

/// @file panels/save_inspector.hpp
/// @brief ST35 — `SaveInspectorPanel`: renders the metadata + record
/// summary for a loaded save file. Optional diff view against a
/// "current" RecordSet so the user can spot what would change after
/// the migration pipeline runs.

#include "../panel.hpp"

#include <cstddef>
#include <string_view>

namespace threadmaxx::migration {
struct RecordSet;
struct SaveMetadata;
struct Record;
} // namespace threadmaxx::migration

namespace threadmaxx::studio {

class SaveInspectorPanel : public IStudioPanel {
public:
    /// @brief Per-record diff outcome between two RecordSets keyed by
    /// stableId. Used by `diff()` and rendered as a short summary.
    struct DiffSummary {
        std::size_t added{0};      ///< only in `current`
        std::size_t removed{0};    ///< only in `loaded`
        std::size_t changed{0};    ///< present in both, different fields
        std::size_t unchanged{0};
    };

    SaveInspectorPanel() noexcept = default;

    /// @brief Bind the panel to a loaded save. Pointer must outlive
    /// the panel; pass `nullptr` to detach.
    void setLoadedSave(const threadmaxx::migration::RecordSet* set) noexcept;

    /// @brief Bind the panel to a "current" baseline used for diff.
    void setCurrentSave(const threadmaxx::migration::RecordSet* set) noexcept;

    /// @brief Compute a diff summary between the two bound sets.
    /// Returns a zeroed summary when either side is unbound.
    [[nodiscard]] DiffSummary diff() const;

    [[nodiscard]] const threadmaxx::migration::RecordSet* loaded() const noexcept {
        return loaded_;
    }
    [[nodiscard]] const threadmaxx::migration::RecordSet* current() const noexcept {
        return current_;
    }

    std::string_view id() const noexcept override {
        return "migration.save_inspector";
    }
    std::string_view title() const noexcept override {
        return "Save Inspector";
    }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    [[nodiscard]] std::size_t rowCount() const noexcept { return lastRows_; }
    void setMaxRows(std::size_t n) noexcept { maxRows_ = n; }

private:
    const threadmaxx::migration::RecordSet* loaded_{nullptr};
    const threadmaxx::migration::RecordSet* current_{nullptr};
    std::size_t                              maxRows_{16};
    std::size_t                              lastRows_{0};
};

} // namespace threadmaxx::studio
