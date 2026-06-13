/// @file panels/MigrationValidatorPanel.cpp
/// @brief ST38 — MigrationValidatorPanel implementation.

#include <threadmaxx_studio/panels/migration_validator.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx_migration/registry.hpp>
#include <threadmaxx_migration/savefile.hpp>
#include <threadmaxx_migration/validation.hpp>

#include <cstdio>
#include <utility>

namespace threadmaxx::studio {

void MigrationValidatorPanel::setRegistry(
        const threadmaxx::migration::MigrationRegistry* reg) noexcept {
    registry_ = reg;
}

void MigrationValidatorPanel::addSave(
        std::string name,
        const threadmaxx::migration::RecordSet* set) {
    CorpusEntry e{};
    e.name = std::move(name);
    e.set  = set;
    corpus_.push_back(std::move(e));
}

void MigrationValidatorPanel::clearCorpus() noexcept {
    corpus_.clear();
    totalWarnings_ = 0;
    totalErrors_   = 0;
}

void MigrationValidatorPanel::runValidation(
        const threadmaxx::migration::SchemaVersion& target) {
    totalWarnings_ = 0;
    totalErrors_   = 0;
    if (registry_ == nullptr) {
        for (auto& entry : corpus_) {
            entry.ok = false;
            entry.warningCount = 0;
            entry.errorCount = 0;
        }
        return;
    }
    for (auto& entry : corpus_) {
        if (entry.set == nullptr) {
            entry.ok = false;
            entry.warningCount = 0;
            entry.errorCount = 0;
            continue;
        }
        const auto report =
            threadmaxx::migration::validate(*entry.set, *registry_, target);
        entry.ok = report.ok;
        entry.warningCount = report.warnings.size();
        entry.errorCount   = report.errors.size();
        totalWarnings_ += entry.warningCount;
        totalErrors_   += entry.errorCount;
    }
}

void MigrationValidatorPanel::render(editor::IEditorBackend& backend,
                                     IStudioDataSource&) {
    char buf[200];
    std::snprintf(buf, sizeof(buf),
                  "Migration Validator  corpus=%zu  warnings=%zu  errors=%zu",
                  corpus_.size(), totalWarnings_, totalErrors_);
    backend.drawText(buf, 0.0f, 0.0f);

    if (registry_ == nullptr) {
        backend.drawText("  <no registry bound>", 0.0f, 16.0f);
        lastRows_ = 2;
        return;
    }

    float y = 16.0f;
    std::size_t shown = 0;
    for (const auto& e : corpus_) {
        if (shown >= maxRows_) break;
        std::snprintf(buf, sizeof(buf),
                      "  [%s] %-24.24s  warn=%zu  err=%zu",
                      e.ok ? "OK " : "BAD",
                      e.name.c_str(),
                      e.warningCount,
                      e.errorCount);
        backend.drawText(buf, 0.0f, y);
        y += 14.0f;
        ++shown;
    }
    lastRows_ = 1 + shown;
}

} // namespace threadmaxx::studio
