/// @file test_studio_save_inspector_diff.cpp
/// @brief ST35 — SaveInspectorPanel diff summary distinguishes
/// added / removed / changed / unchanged records.

#include "Check.hpp"

#include <threadmaxx_studio/panels/save_inspector.hpp>

#include <threadmaxx_editor/backend.hpp>
#include <threadmaxx_editor/backends/headless.hpp>

#include <threadmaxx_migration/records.hpp>
#include <threadmaxx_migration/savefile.hpp>

namespace {

using namespace threadmaxx;

class StubSource final : public studio::IStudioDataSource {
public:
    studio::AttachMode mode() const noexcept override {
        return studio::AttachMode::Direct;
    }
};

migration::Record mk(std::string typeName, std::uint64_t id,
                    std::vector<unsigned char> payload) {
    migration::Record r{};
    r.typeName = std::move(typeName);
    r.stableId = id;
    r.sourceVersion = migration::SchemaVersion{1, 0, 0};
    migration::FieldValue v{};
    for (auto b : payload) v.bytes.push_back(static_cast<std::byte>(b));
    r.fields.push_back({"data", std::move(v)});
    return r;
}

} // namespace

int main() {
    using namespace threadmaxx;

    migration::RecordSet loaded{};
    loaded.metadata.productName = "Demo";
    loaded.metadata.schemaVersion = migration::SchemaVersion{1, 0, 0};
    loaded.records.push_back(mk("Health", 1, {1, 2, 3}));
    loaded.records.push_back(mk("Faction", 2, {0x42}));
    loaded.records.push_back(mk("UserData", 3, {0xAA, 0xBB}));

    migration::RecordSet current{};
    current.metadata.productName = "Demo";
    current.metadata.schemaVersion = migration::SchemaVersion{1, 0, 0};
    // id 1 unchanged.
    current.records.push_back(mk("Health", 1, {1, 2, 3}));
    // id 2 changed.
    current.records.push_back(mk("Faction", 2, {0xFF}));
    // id 3 removed (not present here).
    // id 4 added.
    current.records.push_back(mk("Tag", 4, {0x99}));

    studio::SaveInspectorPanel panel;
    panel.setLoadedSave(&loaded);
    panel.setCurrentSave(&current);

    const auto d = panel.diff();
    CHECK_EQ(d.added, 1u);      // id 4
    CHECK_EQ(d.removed, 1u);    // id 3
    CHECK_EQ(d.changed, 1u);    // id 2
    CHECK_EQ(d.unchanged, 1u);  // id 1

    // Render smoke.
    editor::HeadlessBackend headless;
    CHECK(headless.initialize());
    headless.beginFrame();
    StubSource source;
    panel.render(headless, source);
    CHECK(panel.rowCount() > 0u);

    // Empty state: detach loaded → panel renders the empty header.
    panel.setLoadedSave(nullptr);
    headless.beginFrame();
    panel.render(headless, source);
    CHECK_EQ(panel.rowCount(), 1u);

    // Without a current baseline, diff returns zeros (no comparison
    // possible).
    panel.setLoadedSave(&loaded);
    panel.setCurrentSave(nullptr);
    const auto d2 = panel.diff();
    CHECK_EQ(d2.added, 0u);
    CHECK_EQ(d2.removed, 0u);
    CHECK_EQ(d2.changed, 0u);
    CHECK_EQ(d2.unchanged, 0u);

    EXIT_WITH_RESULT();
}
