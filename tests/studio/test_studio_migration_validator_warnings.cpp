/// @file test_studio_migration_validator_warnings.cpp
/// @brief ST38 — MigrationValidatorPanel runs the validator over a
/// corpus of saves and surfaces aggregated warnings.

#include "Check.hpp"

#include <threadmaxx_studio/panels/migration_validator.hpp>

#include <threadmaxx_editor/backend.hpp>
#include <threadmaxx_editor/backends/headless.hpp>

#include <threadmaxx_migration/records.hpp>
#include <threadmaxx_migration/registry.hpp>
#include <threadmaxx_migration/savefile.hpp>

namespace {

using namespace threadmaxx;

class StubSource final : public studio::IStudioDataSource {
public:
    studio::AttachMode mode() const noexcept override {
        return studio::AttachMode::Direct;
    }
};

migration::Record mkRec(std::string typeName, std::uint64_t id) {
    migration::Record r{};
    r.typeName = std::move(typeName);
    r.stableId = id;
    r.sourceVersion = migration::SchemaVersion{1, 0, 0};
    return r;
}

} // namespace

int main() {
    using namespace threadmaxx;

    migration::MigrationRegistry reg;
    reg.registerType("Health", migration::SchemaVersion{1, 0, 0});

    // Save A: all known.
    migration::RecordSet saveA{};
    saveA.records.push_back(mkRec("Health", 1));
    saveA.records.push_back(mkRec("Health", 2));

    // Save B: two unknown types (Stranger + Outsider) → two warnings.
    migration::RecordSet saveB{};
    saveB.records.push_back(mkRec("Health", 3));
    saveB.records.push_back(mkRec("Stranger", 4));
    saveB.records.push_back(mkRec("Outsider", 5));

    // Save C: known type only.
    migration::RecordSet saveC{};
    saveC.records.push_back(mkRec("Health", 6));

    studio::MigrationValidatorPanel panel;
    panel.setRegistry(&reg);
    panel.addSave("save_a", &saveA);
    panel.addSave("save_b", &saveB);
    panel.addSave("save_c", &saveC);
    CHECK_EQ(panel.corpus().size(), 3u);

    panel.runValidation(migration::SchemaVersion{1, 0, 0});

    // Aggregated: 2 distinct unknowns came from save B; that's
    // 2 warnings overall (totalWarnings = 2; totalErrors = 0).
    CHECK_EQ(panel.totalWarnings(), 2u);
    CHECK_EQ(panel.totalErrors(), 0u);
    CHECK_EQ(panel.lastWarningCount(), 2u);

    // Per-save outcomes.
    const auto& rows = panel.corpus();
    CHECK_EQ(rows.size(), 3u);
    CHECK(rows[0].ok);
    CHECK_EQ(rows[0].warningCount, 0u);
    CHECK(rows[1].ok);            // warnings only, no errors
    CHECK_EQ(rows[1].warningCount, 2u);
    CHECK(rows[2].ok);
    CHECK_EQ(rows[2].warningCount, 0u);

    // Render smoke.
    editor::HeadlessBackend headless;
    CHECK(headless.initialize());
    headless.beginFrame();
    StubSource source;
    panel.render(headless, source);
    CHECK(panel.rowCount() > 0u);

    // clearCorpus zeros everything.
    panel.clearCorpus();
    CHECK_EQ(panel.corpus().size(), 0u);
    CHECK_EQ(panel.totalWarnings(), 0u);

    // Without a registry, runValidation marks every entry as not-ok.
    panel.addSave("orphan", &saveA);
    panel.setRegistry(nullptr);
    panel.runValidation(migration::SchemaVersion{1, 0, 0});
    CHECK_EQ(panel.corpus().size(), 1u);
    CHECK(!panel.corpus()[0].ok);

    EXIT_WITH_RESULT();
}
