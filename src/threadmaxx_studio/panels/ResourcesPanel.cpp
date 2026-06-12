/// @file panels/ResourcesPanel.cpp

#include <threadmaxx_studio/panels/resources.hpp>

#include <threadmaxx_editor/backend.hpp>
#include <threadmaxx_editor/inspect.hpp>

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/Resource.hpp>

#include <cstdio>

namespace threadmaxx::studio {

struct ResourcesPanel::Impl {
    threadmaxx::Subscription sub;
};

ResourcesPanel::ResourcesPanel(threadmaxx::Engine& engine,
                              editor::Inspector& inspector) noexcept
    : impl_(new Impl{}), inspector_(&inspector) {
    impl_->sub = engine.events<threadmaxx::AssetReloaded>().subscribeScoped(
        [this](const threadmaxx::AssetReloaded&) noexcept {
            ++reloadEvents_;
        });
}

ResourcesPanel::~ResourcesPanel() {
    delete impl_;
}

void ResourcesPanel::render(editor::IEditorBackend& backend,
                            IStudioDataSource&) {
    const auto rows = inspector_->listResources();
    lastRowCount_ = rows.size();
    if (rows.empty()) {
        backend.drawText("(no tracked resources)", 0.0f, 0.0f);
        return;
    }
    float y = 0.0f;
    for (const auto& r : rows) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%s [%s] ref=%llu%s",
                      r.name.c_str(), r.typeName.c_str(),
                      static_cast<unsigned long long>(r.refCount),
                      r.stale ? " STALE" : "");
        backend.drawText(buf, 0.0f, y);
        y += 14.0f;
    }
    if (reloadEvents_ > 0) {
        char foot[64];
        std::snprintf(foot, sizeof(foot), "reload events: %llu",
                      static_cast<unsigned long long>(reloadEvents_));
        backend.drawText(foot, 0.0f, y);
    }
}

} // namespace threadmaxx::studio
