#pragma once

/// @file panels/schema_graph.hpp
/// @brief ST37 — `SchemaGraphPanel`: visualizes a `MigrationRegistry`'s
/// per-type migration step graph. One node per registered type,
/// one edge per (type, from, to) step. Renders as a sectioned text
/// table; the panel exposes a graphviz-style DOT snapshot so the
/// host can pipe it through graphviz if desired.

#include "../panel.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace threadmaxx::migration {
class MigrationRegistry;
} // namespace threadmaxx::migration

namespace threadmaxx::studio {

class SchemaGraphPanel : public IStudioPanel {
public:
    SchemaGraphPanel() noexcept = default;

    /// @brief Bind the panel. Pointer must outlive the panel; pass
    /// `nullptr` to detach.
    void setRegistry(const threadmaxx::migration::MigrationRegistry* reg) noexcept;

    /// @brief List of type names the panel will render. Computed
    /// from the bound registry; empty when unbound. The caller
    /// supplies the names because the registry doesn't expose its
    /// internal keys directly — typical use case is `validate()` +
    /// `collectUnknownTypes()` on a loaded RecordSet.
    void setKnownTypeNames(std::vector<std::string> names);

    [[nodiscard]] const std::vector<std::string>&
    knownTypeNames() const noexcept {
        return knownTypeNames_;
    }

    /// @brief Render the registry as a graphviz DOT graph. Useful
    /// for piping into the graphviz CLI for a real diagram, or
    /// embedding in CI artefacts.
    [[nodiscard]] std::string toDot() const;

    std::string_view id() const noexcept override {
        return "migration.schema_graph";
    }
    std::string_view title() const noexcept override {
        return "Schema Graph";
    }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    [[nodiscard]] std::size_t rowCount() const noexcept { return lastRows_; }
    void setMaxRows(std::size_t n) noexcept { maxRows_ = n; }

    /// @brief Edge count rendered last frame. Useful for the test
    /// gate to assert the panel walked every step.
    [[nodiscard]] std::size_t lastEdgeCount() const noexcept {
        return lastEdgeCount_;
    }

private:
    const threadmaxx::migration::MigrationRegistry* registry_{nullptr};
    std::vector<std::string>                         knownTypeNames_;
    std::size_t                                      maxRows_{32};
    std::size_t                                      lastRows_{0};
    std::size_t                                      lastEdgeCount_{0};
};

} // namespace threadmaxx::studio
