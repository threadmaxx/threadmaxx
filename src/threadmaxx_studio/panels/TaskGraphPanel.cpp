/// @file panels/TaskGraphPanel.cpp

#include <threadmaxx_studio/panels/task_graph.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx/Engine.hpp>

#include <cstdio>

namespace threadmaxx::studio {

void TaskGraphPanel::render(editor::IEditorBackend& backend,
                            IStudioDataSource&) {
    const auto nodes = engine_->taskGraphSnapshot();
    lastNodeCount_ = nodes.size();
    lastMaxWave_ = 0;
    if (nodes.empty()) {
        backend.drawText("(no systems registered)", 0.0f, 0.0f);
        return;
    }

    float y = 0.0f;
    for (const auto& n : nodes) {
        if (n.wave > lastMaxWave_) lastMaxWave_ = n.wave;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "[wave %zu] #%zu %s  deps=%zu",
                      n.wave, n.index, n.name.c_str(), n.dependsOn.size());
        backend.drawText(buf, 0.0f, y);
        y += 14.0f;
    }
}

} // namespace threadmaxx::studio
