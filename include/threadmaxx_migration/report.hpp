#pragma once

/// @file report.hpp
/// @brief M5 — `MigrationResult` POD reported by the pipeline.
/// `ok = true` only when every record reached the target version
/// without failing a host-supplied rule (`failOnUnknownType` etc.).
/// Warnings collect lossy / opaque-tombstone outcomes; errors hold
/// the diagnostic that caused the run to abort.

#include "savefile.hpp"

#include <string>
#include <vector>

namespace threadmaxx::migration {

struct MigrationResult {
    bool                     ok{false};
    RecordSet                output;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;

    /// @brief Names of (type, from, to) steps the pipeline actually
    /// invoked. Useful for the studio's MigrationStepPanel (ST36)
    /// and the M8 report summary.
    std::vector<std::string> appliedSteps;
};

/// @brief M8 — Human-readable summary of a `MigrationResult`. The
/// studio's MigrationValidatorPanel (ST38) renders this; the
/// offline convert tool prints it to stdout.
struct MigrationReportSummary {
    std::size_t recordCount{0};
    std::size_t warningCount{0};
    std::size_t errorCount{0};
    std::size_t appliedStepCount{0};
    bool        ok{false};
};

/// @brief Build a summary from a result. Free function so it
/// composes nicely with M5's result type.
[[nodiscard]] inline MigrationReportSummary
summarize(const MigrationResult& result) noexcept {
    MigrationReportSummary out{};
    out.ok               = result.ok;
    out.recordCount      = result.output.records.size();
    out.warningCount     = result.warnings.size();
    out.errorCount       = result.errors.size();
    out.appliedStepCount = result.appliedSteps.size();
    return out;
}

} // namespace threadmaxx::migration
