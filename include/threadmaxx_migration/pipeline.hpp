#pragma once

/// @file pipeline.hpp
/// @brief M5 — `MigrationPipeline`: given a RecordSet at version A,
/// walk the registered migration steps until reaching version B (or
/// `migrateToLatest` resolves B from each record's type registration).
///
/// The pipeline owns a `MigrationRegistry` copy on construction. The
/// caller may pass options to control unknown-type handling, lossy
/// migrations, opaque-tombstone fallback, and stable-id preservation.

#include "registry.hpp"
#include "report.hpp"

#include <utility>

namespace threadmaxx::migration {

struct MigrationOptions {
    /// @brief When true, a step explicitly tagged lossy emits a
    /// warning but does not fail the run. When false, lossy steps
    /// fail the run.
    bool allowLossy{false};
    /// @brief Records whose typeName is unknown to the registry
    /// cause an error when true. When false, they are kept verbatim
    /// (as an opaque tombstone) with a warning.
    bool failOnUnknownType{true};
    /// @brief Reserved for M7 codec integration. When true,
    /// unrecognised fields survive an applied step.
    bool keepUnknownFields{true};
    /// @brief When true, the pipeline asserts that the output
    /// record's `stableId` matches the input. Caught violations are
    /// reported as errors regardless of `failOnUnknownType`.
    bool preserveStableIds{true};
};

class MigrationPipeline {
public:
    explicit MigrationPipeline(MigrationRegistry registry) noexcept
        : registry_(std::move(registry)) {}

    /// @brief Run migrations to the version named by @p target.
    /// Every record in `input.records` is moved through the
    /// per-type chain. `output.metadata` reflects @p target; every
    /// record's `sourceVersion` is set to @p target on success.
    [[nodiscard]] MigrationResult
    migrate(const RecordSet& input,
            SchemaVersion target,
            const MigrationOptions& options = {}) const;

    /// @brief Run migrations to the type's latest registered
    /// version. "Latest" is the maximum `to` across the type's
    /// chain, or the type's introduction version if no steps are
    /// registered.
    [[nodiscard]] MigrationResult
    migrateToLatest(const RecordSet& input,
                    const MigrationOptions& options = {}) const;

    /// @brief Underlying registry reference. Useful for the
    /// MigrationStepPanel + tests.
    [[nodiscard]] const MigrationRegistry& registry() const noexcept {
        return registry_;
    }

private:
    MigrationRegistry registry_;
};

/// @brief Helper: tag a migration as lossy by adding this name as a
/// field on the rewritten Record. The pipeline scans for it on each
/// step's output and folds it into the result's warnings / errors
/// per `MigrationOptions::allowLossy`. The field is stripped before
/// the record is published.
inline constexpr const char* kLossyMarkerField =
    "__migration_lossy_marker__";

} // namespace threadmaxx::migration
