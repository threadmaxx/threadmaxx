/// @file test_studio_schema_graph_render.cpp
/// @brief ST37 — SchemaGraphPanel renders a registry's per-type
/// migration steps; toDot() emits a graphviz DOT snapshot.

#include "Check.hpp"

#include <threadmaxx_studio/panels/schema_graph.hpp>

#include <threadmaxx_editor/backend.hpp>
#include <threadmaxx_editor/backends/headless.hpp>

#include <threadmaxx_migration/registry.hpp>

#include <vector>

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

    migration::MigrationRegistry reg;
    reg.registerType("Health", migration::SchemaVersion{1, 0, 0});
    reg.addMigration("Health",
                     migration::SchemaVersion{1, 0, 0},
                     migration::SchemaVersion{1, 1, 0},
                     [](migration::Record&) {});
    reg.addMigration("Health",
                     migration::SchemaVersion{1, 1, 0},
                     migration::SchemaVersion{2, 0, 0},
                     [](migration::Record&) {});
    reg.registerType("Faction", migration::SchemaVersion{1, 0, 0});
    reg.addMigration("Faction",
                     migration::SchemaVersion{1, 0, 0},
                     migration::SchemaVersion{1, 1, 0},
                     [](migration::Record&) {});

    studio::SchemaGraphPanel panel;
    panel.setRegistry(&reg);
    panel.setKnownTypeNames({"Health", "Faction"});
    CHECK_EQ(panel.knownTypeNames().size(), 2u);

    // Render via headless backend; verify the panel walked every edge.
    editor::HeadlessBackend headless;
    CHECK(headless.initialize());
    headless.beginFrame();
    StubSource source;
    panel.render(headless, source);
    CHECK_EQ(panel.lastEdgeCount(), 3u);  // 2 Health steps + 1 Faction step
    CHECK(panel.rowCount() > 0u);

    // DOT output: contains all 3 directed edges and the digraph header.
    const auto dot = panel.toDot();
    CHECK(dot.find("digraph SchemaGraph") != std::string::npos);
    CHECK(dot.find("Health@1.0.0") != std::string::npos);
    CHECK(dot.find("Health@1.1.0") != std::string::npos);
    CHECK(dot.find("Health@2.0.0") != std::string::npos);
    CHECK(dot.find("Faction@1.0.0") != std::string::npos);
    CHECK(dot.find("Faction@1.1.0") != std::string::npos);
    // The directed edges are present.
    CHECK(dot.find("\"Health@1.0.0\" -> \"Health@1.1.0\"") != std::string::npos);
    CHECK(dot.find("\"Health@1.1.0\" -> \"Health@2.0.0\"") != std::string::npos);
    CHECK(dot.find("\"Faction@1.0.0\" -> \"Faction@1.1.0\"") != std::string::npos);

    // Detach: empty header.
    panel.setRegistry(nullptr);
    headless.beginFrame();
    panel.render(headless, source);
    CHECK_EQ(panel.lastEdgeCount(), 0u);
    CHECK_EQ(panel.rowCount(), 1u);

    EXIT_WITH_RESULT();
}
