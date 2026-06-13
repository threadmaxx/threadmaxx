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

} // namespace threadmaxx::migration
