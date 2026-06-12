#pragma once

/// @file panels/task_graph.hpp
/// @brief `TaskGraphPanel` — renders `Engine::taskGraphSnapshot()`
/// as a wave-grouped node list. Future ST batches add edge rendering
/// + Graphviz export; v0 is the row-per-system view.

#include "../panel.hpp"

#include <string_view>

namespace threadmaxx {
class Engine;
} // namespace threadmaxx

namespace threadmaxx::studio {

class TaskGraphPanel : public IStudioPanel {
public:
    explicit TaskGraphPanel(threadmaxx::Engine& engine) noexcept
        : engine_(&engine) {}

    std::string_view id() const noexcept override {
        return "engine.task_graph";
    }
    std::string_view title() const noexcept override { return "Task Graph"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief Node count from the most recent render. Tests inspect
    /// this without re-running the snapshot.
    std::size_t lastNodeCount() const noexcept { return lastNodeCount_; }

    /// @brief Highest wave index seen on the most recent render
    /// (0-based). Tests assert grouping shape.
    std::size_t lastMaxWave() const noexcept { return lastMaxWave_; }

private:
    threadmaxx::Engine* engine_;
    std::size_t lastNodeCount_{0};
    std::size_t lastMaxWave_{0};
};

} // namespace threadmaxx::studio
