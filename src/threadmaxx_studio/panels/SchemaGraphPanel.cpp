/// @file panels/SchemaGraphPanel.cpp
/// @brief ST37 — SchemaGraphPanel implementation.

#include <threadmaxx_studio/panels/schema_graph.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx_migration/registry.hpp>

#include <cstdio>
#include <sstream>
#include <utility>

namespace threadmaxx::studio {

void SchemaGraphPanel::setRegistry(
        const threadmaxx::migration::MigrationRegistry* reg) noexcept {
    registry_ = reg;
}

void SchemaGraphPanel::setKnownTypeNames(std::vector<std::string> names) {
    knownTypeNames_ = std::move(names);
}

std::string SchemaGraphPanel::toDot() const {
    std::ostringstream out;
    out << "digraph SchemaGraph {\n";
    out << "  rankdir=LR;\n";
    if (registry_ != nullptr) {
        for (const auto& typeName : knownTypeNames_) {
            const auto steps = registry_->listSteps(typeName);
            for (const auto& step : steps) {
                char buf[80];
                std::snprintf(buf, sizeof(buf),
                              "  \"%s@%u.%u.%u\" -> \"%s@%u.%u.%u\";\n",
                              typeName.c_str(),
                              step.from.major, step.from.minor, step.from.patch,
                              typeName.c_str(),
                              step.to.major,   step.to.minor,   step.to.patch);
                out << buf;
            }
        }
    }
    out << "}\n";
    return out.str();
}

void SchemaGraphPanel::render(editor::IEditorBackend& backend,
                              IStudioDataSource&) {
    char buf[200];
    if (registry_ == nullptr) {
        backend.drawText("Schema Graph: <no registry bound>", 0.0f, 0.0f);
        lastRows_ = 1;
        lastEdgeCount_ = 0;
        return;
    }

    std::snprintf(buf, sizeof(buf),
                  "Schema Graph  types=%zu",
                  knownTypeNames_.size());
    backend.drawText(buf, 0.0f, 0.0f);

    float y = 16.0f;
    std::size_t shown = 0;
    std::size_t edgeCount = 0;
    for (const auto& typeName : knownTypeNames_) {
        if (shown >= maxRows_) break;
        const auto steps = registry_->listSteps(typeName);
        std::snprintf(buf, sizeof(buf),
                      "  %s (intro=%u.%u.%u, steps=%zu)",
                      typeName.c_str(),
                      registry_->introducedAt(typeName).major,
                      registry_->introducedAt(typeName).minor,
                      registry_->introducedAt(typeName).patch,
                      steps.size());
        backend.drawText(buf, 0.0f, y);
        y += 14.0f;
        ++shown;
        for (const auto& step : steps) {
            if (shown >= maxRows_) break;
            std::snprintf(buf, sizeof(buf),
                          "    %u.%u.%u -> %u.%u.%u",
                          step.from.major, step.from.minor, step.from.patch,
                          step.to.major,   step.to.minor,   step.to.patch);
            backend.drawText(buf, 0.0f, y);
            y += 14.0f;
            ++shown;
            ++edgeCount;
        }
    }
    lastRows_ = 1 + shown;
    lastEdgeCount_ = edgeCount;
}

} // namespace threadmaxx::studio
