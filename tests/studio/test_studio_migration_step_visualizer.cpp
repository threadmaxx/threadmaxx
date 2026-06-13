/// @file test_studio_migration_step_visualizer.cpp
/// @brief ST36 — MigrationStepPanel walks through a pipeline result's
/// applied-step list one entry at a time.

#include "Check.hpp"

#include <threadmaxx_studio/panels/migration_step.hpp>

#include <threadmaxx_editor/backend.hpp>
#include <threadmaxx_editor/backends/headless.hpp>

#include <threadmaxx_migration/pipeline.hpp>

namespace {

using namespace threadmaxx;

class StubSource final : public studio::IStudioDataSource {
public:
    studio::AttachMode mode() const noexcept override {
        return studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    using namespace threadmaxx;

    // Build a pipeline that emits three applied steps.
    migration::MigrationRegistry reg;
    reg.registerType("Health", migration::SchemaVersion{1, 0, 0});
    reg.addMigration("Health", migration::SchemaVersion{1, 0, 0},
                     migration::SchemaVersion{1, 1, 0},
                     [](migration::Record&) {});
    reg.addMigration("Health", migration::SchemaVersion{1, 1, 0},
                     migration::SchemaVersion{1, 2, 0},
                     [](migration::Record&) {});
    reg.addMigration("Health", migration::SchemaVersion{1, 2, 0},
                     migration::SchemaVersion{2, 0, 0},
                     [](migration::Record&) {});

    migration::MigrationPipeline pipeline{reg};
    migration::RecordSet input{};
    migration::Record r{};
    r.typeName = "Health";
    r.stableId = 1;
    r.sourceVersion = migration::SchemaVersion{1, 0, 0};
    input.records.push_back(r);

    auto result = pipeline.migrate(input, migration::SchemaVersion{2, 0, 0});
    CHECK(result.ok);
    CHECK_EQ(result.appliedSteps.size(), 3u);

    studio::MigrationStepPanel panel;
    CHECK_EQ(panel.stepCount(), 0u);  // unbound

    panel.setResult(&result);
    CHECK_EQ(panel.stepCount(), 3u);
    CHECK_EQ(panel.cursor(), 0u);

    // Walk through the steps.
    CHECK_EQ(panel.stepForward(), 1u);
    CHECK_EQ(panel.cursor(), 1u);
    CHECK_EQ(panel.stepForward(), 2u);
    CHECK_EQ(panel.stepForward(), 3u);
    // Clamped at the end.
    CHECK_EQ(panel.stepForward(), 3u);

    // Walk back.
    CHECK_EQ(panel.stepBackward(), 2u);
    CHECK_EQ(panel.stepBackward(), 1u);
    CHECK_EQ(panel.stepBackward(), 0u);
    // Clamped at the start.
    CHECK_EQ(panel.stepBackward(), 0u);

    panel.stepForward();
    panel.rewind();
    CHECK_EQ(panel.cursor(), 0u);

    // Render smoke.
    editor::HeadlessBackend headless;
    CHECK(headless.initialize());
    headless.beginFrame();
    StubSource source;
    panel.render(headless, source);
    CHECK(panel.rowCount() > 0u);

    // Detach: panel renders the empty header.
    panel.setResult(nullptr);
    CHECK_EQ(panel.stepCount(), 0u);
    CHECK_EQ(panel.cursor(), 0u);
    headless.beginFrame();
    panel.render(headless, source);
    CHECK_EQ(panel.rowCount(), 1u);

    EXIT_WITH_RESULT();
}
