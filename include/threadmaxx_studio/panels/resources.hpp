#pragma once

/// @file panels/resources.hpp
/// @brief `ResourcesPanel` — wraps editor's `Inspector::listResources`
/// + the engine's `AssetReloaded` event stream. Tracks reload counts
/// per type so hosts can verify hot-reload actually fired.

#include "../panel.hpp"

#include <cstddef>
#include <string_view>

namespace threadmaxx::editor {
class Inspector;
} // namespace threadmaxx::editor

namespace threadmaxx {
class Engine;
} // namespace threadmaxx

namespace threadmaxx::studio {

class ResourcesPanel : public IStudioPanel {
public:
    ResourcesPanel(threadmaxx::Engine& engine,
                   editor::Inspector& inspector) noexcept;
    ~ResourcesPanel() override;

    ResourcesPanel(const ResourcesPanel&) = delete;
    ResourcesPanel& operator=(const ResourcesPanel&) = delete;

    std::string_view id() const noexcept override {
        return "engine.resources";
    }
    std::string_view title() const noexcept override { return "Resources"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief Count of rows from the most recent `render()`.
    std::size_t lastRowCount() const noexcept { return lastRowCount_; }

    /// @brief Cumulative `AssetReloaded` events the panel has
    /// observed since construction.
    std::uint64_t reloadEventCount() const noexcept { return reloadEvents_; }

private:
    struct Impl;
    Impl* impl_;
    editor::Inspector* inspector_;
    std::size_t lastRowCount_{0};
    std::uint64_t reloadEvents_{0};
};

} // namespace threadmaxx::studio
