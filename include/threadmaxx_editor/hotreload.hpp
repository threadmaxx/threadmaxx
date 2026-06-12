#pragma once

/// @file hotreload.hpp
/// @brief Editor-side orchestration of the engine's hot-reload pipeline.
///
/// The engine owns `Engine::markResourceStale<T>` and the
/// `AssetReloaded` event channel. The controller is the editor-side
/// glue: it tracks which resources the editor cares about (by path),
/// kicks off staleness requests on game-side request, and observes
/// completion via an `AssetReloaded` subscription. It does NOT watch
/// the filesystem — that's game-side / OS-specific and lives outside
/// this library.

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/Resource.hpp>

namespace threadmaxx::editor {

/// @brief One reload request from game / editor code.
struct ReloadRequest {
    std::string resourcePath;
    bool forceReimport{false};
};

/// @brief Synchronous result of `requestReload`. Detail is editorial —
/// the actual reload completes asynchronously, signaled by an
/// `AssetReloaded` event.
struct ReloadResult {
    bool ok{false};
    std::string message;
};

/// @brief Editor-side hot-reload coordinator. Construct once per
/// editor session; lifetime should match the session.
///
/// @thread_safety `requestReload` / `cancelReload` / `pendingReloads`
/// may be called from any thread. The internal `AssetReloaded`
/// subscription fires on the engine's sim thread during event drain
/// (see `EventChannel::drain`).
class HotReloadController {
public:
    explicit HotReloadController(threadmaxx::Engine& engine);
    ~HotReloadController();

    HotReloadController(const HotReloadController&) = delete;
    HotReloadController& operator=(const HotReloadController&) = delete;
    HotReloadController(HotReloadController&&) = delete;
    HotReloadController& operator=(HotReloadController&&) = delete;

    /// @brief Register a resource id under an editor-facing path.
    ///
    /// Once registered, `requestReload({path})` knows which engine
    /// resource to mark stale, and the controller's `AssetReloaded`
    /// subscription knows which path to clear on completion.
    template <typename T>
    void trackResource(threadmaxx::ResourceId<T> id, std::string path) {
        std::lock_guard<std::mutex> lk(mtx_);
        tracked_.push_back(TrackedResource{
            std::move(path),
            id.index, id.generation,
            std::type_index(typeid(T)),
            &HotReloadController::markStaleFor<T>,
        });
    }

    /// @brief Queue a reload for `request.resourcePath`. Returns
    /// `{ok=true}` if the path is tracked, `{ok=false, message}`
    /// otherwise. The engine's loader picks up the stale id on its
    /// next `update()` tick.
    ReloadResult requestReload(const ReloadRequest& request);

    /// @brief Drop a path from the pending list. Idempotent.
    void cancelReload(std::string_view resourcePath);

    /// @brief Snapshot of paths currently waiting for an
    /// `AssetReloaded` event.
    std::vector<std::string> pendingReloads() const;

    /// @brief Number of currently-tracked resource ids.
    std::size_t trackedCount() const noexcept;

private:
    struct TrackedResource {
        std::string path;
        std::uint32_t index;
        std::uint32_t generation;
        std::type_index type;
        // Type-erased markResourceStale<T>(engine, id) trampoline.
        void (*markStaleFn)(threadmaxx::Engine&,
                            std::uint32_t, std::uint32_t);
    };

    template <typename T>
    static void markStaleFor(threadmaxx::Engine& engine,
                             std::uint32_t index,
                             std::uint32_t generation) {
        engine.markResourceStale<T>(threadmaxx::ResourceId<T>{index, generation});
    }

    void onAssetReloaded_(const threadmaxx::AssetReloaded& evt);

    threadmaxx::Engine* engine_;
    mutable std::mutex mtx_;
    std::vector<TrackedResource> tracked_;
    std::vector<std::string> pending_;
    threadmaxx::Subscription sub_;
};

} // namespace threadmaxx::editor
