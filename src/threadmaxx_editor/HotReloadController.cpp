/// @file HotReloadController.cpp
/// @brief Editor-side hot-reload orchestration.

#include "threadmaxx_editor/hotreload.hpp"

#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/Resource.hpp>

#include <algorithm>

namespace threadmaxx::editor {

HotReloadController::HotReloadController(threadmaxx::Engine& engine)
    : engine_(&engine) {
    sub_ = engine.events<threadmaxx::AssetReloaded>().subscribeScoped(
        [this](const threadmaxx::AssetReloaded& evt) {
            onAssetReloaded_(evt);
        });
}

HotReloadController::~HotReloadController() = default;

ReloadResult
HotReloadController::requestReload(const ReloadRequest& request) {
    TrackedResource* found = nullptr;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& t : tracked_) {
            if (t.path == request.resourcePath) {
                found = &t;
                break;
            }
        }
        if (!found) {
            return {false, std::string("not tracked: ") + request.resourcePath};
        }
        // De-dupe; ignore duplicate pending entries.
        if (std::find(pending_.begin(), pending_.end(),
                      request.resourcePath) == pending_.end()) {
            pending_.push_back(request.resourcePath);
        }
    }
    found->markStaleFn(*engine_, found->index, found->generation);
    (void)request.forceReimport; // editorial; loader decides.
    return {true, {}};
}

void HotReloadController::cancelReload(std::string_view resourcePath) {
    std::lock_guard<std::mutex> lk(mtx_);
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
        [&](const std::string& p) { return p == resourcePath; }),
        pending_.end());
}

std::vector<std::string> HotReloadController::pendingReloads() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return pending_;
}

std::size_t HotReloadController::trackedCount() const noexcept {
    std::lock_guard<std::mutex> lk(mtx_);
    return tracked_.size();
}

void HotReloadController::onAssetReloaded_(
        const threadmaxx::AssetReloaded& evt) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto& t : tracked_) {
        if (t.type == evt.type &&
            t.index == evt.oldIndex &&
            t.generation == evt.oldGeneration) {
            // Bind the tracked entry to the new id so subsequent
            // request/cancel ops continue to work.
            t.index = evt.newIndex;
            t.generation = evt.newGeneration;
            pending_.erase(std::remove(pending_.begin(), pending_.end(),
                                       t.path),
                           pending_.end());
            break;
        }
    }
}

} // namespace threadmaxx::editor
