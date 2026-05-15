#pragma once

#include "VulkanContext.hpp"

#include <threadmaxx/Resource.hpp>
#include <threadmaxx_vk/Mesh.hpp>  // Shader POD lives here

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>
#include <typeindex>
#include <vector>

namespace threadmaxx_vk {

/// Shader bytecode loader with hot-reload support.
///
/// Drives the §3.2-batch-7 reload contract end-to-end:
///   1. Game code (or a file watcher) calls
///      `Engine::markResourceStale<Shader>(id)`.
///   2. The engine fans out to this loader via @ref markStale.
///   3. Next `update()` tick on the sim thread: this loader reads the
///      shader from disk, builds a new registry entry, and emits
///      `AssetReloaded` so subscribers can rewire.
///
/// The renderer is the canonical subscriber: it tears down the
/// pipeline owning the old shader and rebuilds it from the new bytes.
///
/// File path is tracked alongside the resource id. The loader caches
/// the bytes in @ref ownedSpirv_ so destruction is straightforward.
class ShaderLoader : public threadmaxx::IResourceLoader {
public:
    explicit ShaderLoader(VulkanContext& ctx) : ctx_(ctx) {}
    ~ShaderLoader() override = default;

    void update(threadmaxx::Engine& engine) override;
    void onShutdown(threadmaxx::Engine& engine) override;

    threadmaxx::LoaderStats stats() const noexcept override;

    void markStale(std::uint32_t index,
                   std::uint32_t generation,
                   std::type_index type) override;

    /// Register a shader and remember the path that should be re-read on
    /// hot reload. Pass an embedded byte span when no file backing
    /// exists (the loader still tracks the id but `markStale` becomes a
    /// no-op for it). Idempotent on `path`.
    threadmaxx::ResourceHandle<Shader> add(threadmaxx::Engine& engine,
                                           std::filesystem::path path,
                                           std::span<const std::uint32_t> spirv);

private:
    struct Entry {
        threadmaxx::ResourceId<Shader> id;
        std::filesystem::path          path;
    };

    /// Records that `id` should be re-read from `path` on the next
    /// `update()`.
    void queueReload_(threadmaxx::ResourceId<Shader> id);

    VulkanContext& ctx_;
    std::mutex                                  mtx_;
    std::vector<Entry>                          entries_;
    std::vector<threadmaxx::ResourceId<Shader>> pendingReloads_;
    std::atomic<std::uint64_t>                  resident_ = 0;
    std::atomic<std::uint64_t>                  pendingCount_ = 0;
};

} // namespace threadmaxx_vk
