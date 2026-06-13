/// @file panels/MigrationStepPanel.cpp
/// @brief ST36 — MigrationStepPanel implementation.

#include <threadmaxx_studio/panels/migration_step.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx_migration/report.hpp>

#include <cstdio>

namespace threadmaxx::studio {

void MigrationStepPanel::setResult(
        const threadmaxx::migration::MigrationResult* result) noexcept {
    result_ = result;
    cursor_ = 0;
}

std::size_t MigrationStepPanel::stepCount() const noexcept {
    if (result_ == nullptr) return 0;
    return result_->appliedSteps.size();
}

std::size_t MigrationStepPanel::stepForward() noexcept {
    const auto n = stepCount();
    if (n == 0) return 0;
    if (cursor_ < n) ++cursor_;
    return cursor_;
}

std::size_t MigrationStepPanel::stepBackward() noexcept {
    if (cursor_ > 0) --cursor_;
    return cursor_;
}

void MigrationStepPanel::render(editor::IEditorBackend& backend,
                                IStudioDataSource&) {
    char buf[200];
    if (result_ == nullptr) {
        backend.drawText("Migration Steps: <no result bound>", 0.0f, 0.0f);
        lastRows_ = 1;
        return;
    }

    const auto total = stepCount();
    std::snprintf(buf, sizeof(buf),
                  "Migration Steps  applied=%zu  cursor=%zu  ok=%d  "
                  "warnings=%zu  errors=%zu",
                  total, cursor_, result_->ok ? 1 : 0,
                  result_->warnings.size(), result_->errors.size());
    backend.drawText(buf, 0.0f, 0.0f);

    float y = 16.0f;
    std::size_t shown = 0;
    for (std::size_t i = 0; i < result_->appliedSteps.size(); ++i) {
        if (shown >= maxRows_) break;
        const char marker = (i + 1 == cursor_) ? '>' : ' ';
        std::snprintf(buf, sizeof(buf),
                      "%c [%zu] %s",
                      marker, i, result_->appliedSteps[i].c_str());
        backend.drawText(buf, 0.0f, y);
        y += 14.0f;
        ++shown;
    }
    lastRows_ = 1 + shown;
}

} // namespace threadmaxx::studio
