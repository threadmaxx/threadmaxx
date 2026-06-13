/// @file test_studio_reflect_panel_list_types.cpp
/// @brief ST22 — ReflectPanel renders type rows; `selectType(name)`
/// expands into per-field rows. Placeholder when unbound.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/reflect.hpp>

#include <threadmaxx_reflect/macro.hpp>
#include <threadmaxx_reflect/registry.hpp>

#include <cstdint>

namespace {

struct Health {
    float        current;
    float        max;
    std::int32_t regen;
};
THREADMAXX_REFLECT(Health, current, max, regen);

struct Wallet {
    std::uint64_t gold;
    std::uint32_t gems;
};
THREADMAXX_REFLECT(Wallet, gold, gems);

std::size_t countTextOps(
    const threadmaxx::editor::CapturedFrame& frame) {
    std::size_t n = 0;
    for (const auto& op : frame.ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText) ++n;
    }
    return n;
}

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    using namespace threadmaxx;
    studio::ReflectPanel panel;

    editor::HeadlessBackend backend;
    backend.initialize();
    NullSource source;

    // Detached placeholder.
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.typeRowCount(), 0u);
    CHECK_EQ(panel.fieldRowCount(), 0u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 1u);

    reflect::TypeRegistry reg;
    [[maybe_unused]] auto* hType = reg.registerType<Health>("Health");
    [[maybe_unused]] auto* wType = reg.registerType<Wallet>("Wallet");
    panel.setRegistry(&reg);

    // No selection — header + 2 type rows; no field rows.
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.typeRowCount(), 2u);
    CHECK_EQ(panel.fieldRowCount(), 0u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 3u);

    // Select Health → +1 header row ("Fields of Health:") + 3 fields.
    panel.selectType("Health");
    CHECK(panel.selectedType() == "Health");
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.typeRowCount(), 2u);
    CHECK_EQ(panel.fieldRowCount(), 3u);
    // 1 header + 2 type rows + 1 "Fields of" subheader + 3 field rows.
    CHECK_EQ(countTextOps(backend.capturedFrame()), 7u);

    // Selecting an unknown type leaves the type list intact but emits
    // no field rows.
    panel.selectType("Nonexistent");
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.typeRowCount(), 2u);
    CHECK_EQ(panel.fieldRowCount(), 0u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 3u);

    backend.shutdown();
    EXIT_WITH_RESULT();
}
