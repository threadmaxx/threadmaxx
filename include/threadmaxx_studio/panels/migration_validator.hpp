#pragma once

/// @file panels/migration_validator.hpp
/// @brief ST38 — `MigrationValidatorPanel`: runs the validator from
/// `threadmaxx_migration/validation.hpp` over a corpus of save files
/// (one `RecordSet` per slot) against a target schema and a registry.
/// Surfaces aggregated warnings + per-save outcomes.

#include "../panel.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace threadmaxx::migration {
class MigrationRegistry;
struct RecordSet;
struct SchemaVersion;
} // namespace threadmaxx::migration

namespace threadmaxx::studio {

class MigrationValidatorPanel : public IStudioPanel {
public:
    /// @brief One row of the per-save outcome table.
    struct CorpusEntry {
        std::string name;
        const threadmaxx::migration::RecordSet* set{nullptr};
        bool        ok{false};
        std::size_t warningCount{0};
        std::size_t errorCount{0};
    };

    MigrationValidatorPanel() noexcept = default;

    /// @brief Bind a registry. Pointer must outlive the panel.
    void setRegistry(const threadmaxx::migration::MigrationRegistry* reg) noexcept;

    /// @brief Register a save file in the corpus. Stores the pointer
    /// + name; the panel never copies the RecordSet itself.
    void addSave(std::string name,
                 const threadmaxx::migration::RecordSet* set);

    /// @brief Clear the corpus + the last-run table.
    void clearCorpus() noexcept;

    /// @brief Run the validator over every registered save against
    /// @p target. Populates the per-save outcome table.
    void runValidation(const threadmaxx::migration::SchemaVersion& target);

    [[nodiscard]] const std::vector<CorpusEntry>& corpus() const noexcept {
        return corpus_;
    }

    [[nodiscard]] std::size_t totalWarnings() const noexcept {
        return totalWarnings_;
    }
    [[nodiscard]] std::size_t totalErrors() const noexcept {
        return totalErrors_;
    }
    [[nodiscard]] std::size_t lastWarningCount() const noexcept {
        return totalWarnings_;
    }

    std::string_view id() const noexcept override {
        return "migration.validator";
    }
    std::string_view title() const noexcept override {
        return "Migration Validator";
    }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    [[nodiscard]] std::size_t rowCount() const noexcept { return lastRows_; }
    void setMaxRows(std::size_t n) noexcept { maxRows_ = n; }

private:
    const threadmaxx::migration::MigrationRegistry* registry_{nullptr};
    std::vector<CorpusEntry> corpus_;
    std::size_t              totalWarnings_{0};
    std::size_t              totalErrors_{0};
    std::size_t              maxRows_{16};
    std::size_t              lastRows_{0};
};

} // namespace threadmaxx::studio
