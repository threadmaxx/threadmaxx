#pragma once

#include <cstddef>
#include <memory>
#include <string_view>

#include "data/audio.hpp"
#include "data/font.hpp"
#include "data/mesh.hpp"
#include "data/texture.hpp"
#include "registry.hpp"

namespace threadmaxx::assets {

// Async asset loader pool. Workers consume queued load requests off-thread
// and stage completed records in a thread-safe done queue. `pump()` is
// called from the consumer thread (typically the sim thread) to install
// completed records into the registry. Worker count defaults to 2.
class AsyncLoader {
public:
    explicit AsyncLoader(AssetRegistry& reg,
                         std::size_t workerCount = 0);
    ~AsyncLoader();
    AsyncLoader(const AsyncLoader&) = delete;
    AsyncLoader& operator=(const AsyncLoader&) = delete;

    // Enqueue a load. Returns an invalid handle that flips to valid after
    // pump() observes the request complete and installs into the registry.
    // Identical canonical paths dedup against existing registry slots
    // synchronously (no work queued).
    AssetHandle<MeshData>      enqueueMesh   (std::string_view path);
    AssetHandle<TextureData>   enqueueTexture(std::string_view path);
    AssetHandle<AudioClipData> enqueueAudio  (std::string_view path);
    AssetHandle<FontAtlas>     enqueueFont   (std::string_view path);

    // Drains completed records on the calling thread. Zero-alloc fast
    // path when nothing is ready.
    void pump();

    [[nodiscard]] std::size_t pendingCount() const noexcept;
    [[nodiscard]] std::size_t inFlightCount() const noexcept;
    [[nodiscard]] std::size_t failedCount() const noexcept;
    [[nodiscard]] std::size_t workerCount() const noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace threadmaxx::assets
