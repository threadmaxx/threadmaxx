#include "threadmaxx_assets/async_loader.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "threadmaxx_assets/config.hpp"
#include "threadmaxx_assets/loaders/bmfont.hpp"
#include "threadmaxx_assets/loaders/bmp.hpp"
#include "threadmaxx_assets/loaders/obj.hpp"
#include "threadmaxx_assets/loaders/ply.hpp"
#include "threadmaxx_assets/loaders/png.hpp"
#include "threadmaxx_assets/loaders/tga.hpp"
#include "threadmaxx_assets/loaders/wav.hpp"

namespace threadmaxx::assets {

namespace {

enum class WorkType : std::uint8_t {
    Mesh, Texture, Audio, Font
};

struct DoneRecord {
    WorkType type{};
    std::string path;        // original key
    bool       ok{false};
    // Exactly one of these is populated.
    MeshData      mesh;
    TextureData   texture;
    AudioClipData audio;
    FontAtlas     font;
};

} // namespace

struct AsyncLoader::Impl {
    AssetRegistry&                       registry;
    std::vector<std::thread>             workers;
    mutable std::mutex                   qMu;
    std::condition_variable              qCv;
    std::vector<std::function<void()>>   queue;
    mutable std::mutex                   doneMu;
    std::vector<DoneRecord>              done;
    std::atomic<std::size_t>             inFlight{0};
    std::atomic<std::size_t>             failed{0};
    std::atomic<bool>                    stop{false};

    // Pump installs the typed PODs in the registry; the returned handles
    // each carry a refcount the loader keeps alive so the slot doesn't get
    // freed before the consumer can claim it via findX. The loader's own
    // lifetime bounds the assets it produced; the consumer's findX handle
    // then owns the lifetime independently.
    std::vector<AssetHandle<MeshData>>      ownedMeshes;
    std::vector<AssetHandle<TextureData>>   ownedTextures;
    std::vector<AssetHandle<AudioClipData>> ownedAudio;
    std::vector<AssetHandle<FontAtlas>>     ownedFonts;

    Impl(AssetRegistry& reg) : registry(reg) {}

    void enqueueRaw(std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lk(qMu);
            queue.push_back(std::move(fn));
        }
        qCv.notify_one();
    }

    void pushDone(DoneRecord rec) {
        std::lock_guard<std::mutex> lk(doneMu);
        done.push_back(std::move(rec));
    }
};

AsyncLoader::AsyncLoader(AssetRegistry& reg, std::size_t workerCount)
    : impl_(std::make_unique<Impl>(reg)) {
    if (workerCount == 0) workerCount = kAsyncLoaderDefaultWorkers;
    impl_->workers.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; ++i) {
        impl_->workers.emplace_back([impl = impl_.get()]() {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lk(impl->qMu);
                    impl->qCv.wait(lk, [impl] {
                        return impl->stop.load(std::memory_order_acquire) ||
                               !impl->queue.empty();
                    });
                    if (impl->stop.load(std::memory_order_acquire) && impl->queue.empty()) {
                        return;
                    }
                    task = std::move(impl->queue.back());
                    impl->queue.pop_back();
                }
                if (task) task();
            }
        });
    }
}

AsyncLoader::~AsyncLoader() {
    impl_->stop.store(true, std::memory_order_release);
    impl_->qCv.notify_all();
    for (auto& t : impl_->workers) {
        if (t.joinable()) t.join();
    }
}

namespace {

template <class T, class LoadFn>
AssetHandle<T> enqueueTemplated(AsyncLoader::Impl& impl,
                                AssetRegistry& reg,
                                std::string_view path,
                                WorkType type,
                                LoadFn fn,
                                AssetHandle<T>(AssetRegistry::*findExisting)(std::string_view)) {
    // Existing-slot dedup (no I/O). If the registry already has the
    // record (sync-loaded or async-installed by an earlier pump), return
    // it; nothing is queued.
    auto h = (reg.*findExisting)(path);
    if (h.valid()) return h;

    // Otherwise, queue actual disk work. Canonicalize the path so the
    // pump's addX(canonical, value) call lines up with the same
    // canonicalized key that registry sync loads would use.
    std::error_code ec;
    auto canon = std::filesystem::weakly_canonical(
        std::filesystem::path(path), ec);
    std::string p = ec ? std::string(path) : canon.string();
    impl.inFlight.fetch_add(1, std::memory_order_relaxed);
    impl.enqueueRaw([impl_ptr = &impl, type, fn, p]() mutable {
        auto r = fn(p);
        DoneRecord rec;
        rec.type = type;
        rec.path = std::move(p);
        if (r.ok()) {
            rec.ok = true;
            if constexpr (std::is_same_v<T, MeshData>)      rec.mesh    = std::move(r.value);
            else if constexpr (std::is_same_v<T, TextureData>)  rec.texture = std::move(r.value);
            else if constexpr (std::is_same_v<T, AudioClipData>) rec.audio = std::move(r.value);
            else                                                rec.font  = std::move(r.value);
        } else {
            rec.ok = false;
            impl_ptr->failed.fetch_add(1, std::memory_order_relaxed);
        }
        impl_ptr->pushDone(std::move(rec));
        impl_ptr->inFlight.fetch_sub(1, std::memory_order_acq_rel);
    });
    // Return an invalid handle for now; pump() will swap a valid one in
    // later when the caller next re-queries the registry.
    return AssetHandle<T>{};
}

} // namespace

AssetHandle<MeshData> AsyncLoader::enqueueMesh(std::string_view path) {
    return enqueueTemplated<MeshData>(*impl_, impl_->registry, path,
                                      WorkType::Mesh,
                                      [](const std::string& p) -> AssetResult<MeshData> {
        const auto ext = std::filesystem::path(p).extension().string();
        if (ext == ".ply" || ext == ".PLY") return loadPly(p);
        return loadObj(p);
    }, &AssetRegistry::findMesh);
}
AssetHandle<TextureData> AsyncLoader::enqueueTexture(std::string_view path) {
    return enqueueTemplated<TextureData>(*impl_, impl_->registry, path,
                                         WorkType::Texture,
                                         [](const std::string& p) -> AssetResult<TextureData> {
        const auto ext = std::filesystem::path(p).extension().string();
        if (ext == ".bmp" || ext == ".BMP") return loadBmp(p);
        if (ext == ".tga" || ext == ".TGA") return loadTga(p);
        return loadPng(p);
    }, &AssetRegistry::findTexture);
}
AssetHandle<AudioClipData> AsyncLoader::enqueueAudio(std::string_view path) {
    return enqueueTemplated<AudioClipData>(*impl_, impl_->registry, path,
                                           WorkType::Audio,
                                           [](const std::string& p) { return loadWav(p); },
                                           &AssetRegistry::findAudio);
}
AssetHandle<FontAtlas> AsyncLoader::enqueueFont(std::string_view path) {
    return enqueueTemplated<FontAtlas>(*impl_, impl_->registry, path,
                                       WorkType::Font,
                                       [](const std::string& p) { return loadBmfont(p); },
                                       &AssetRegistry::findFont);
}

void AsyncLoader::pump() {
    // Steal done records without holding the lock during installation.
    std::vector<DoneRecord> local;
    {
        std::lock_guard<std::mutex> lk(impl_->doneMu);
        if (impl_->done.empty()) return; // zero-alloc fast path
        local.swap(impl_->done);
    }
    for (auto& rec : local) {
        if (!rec.ok) continue;
        switch (rec.type) {
            case WorkType::Mesh:
                impl_->ownedMeshes.push_back(
                    impl_->registry.addMesh(rec.path, std::move(rec.mesh)));
                break;
            case WorkType::Texture:
                impl_->ownedTextures.push_back(
                    impl_->registry.addTexture(rec.path, std::move(rec.texture)));
                break;
            case WorkType::Audio:
                impl_->ownedAudio.push_back(
                    impl_->registry.addAudio(rec.path, std::move(rec.audio)));
                break;
            case WorkType::Font:
                impl_->ownedFonts.push_back(
                    impl_->registry.addFont(rec.path, std::move(rec.font)));
                break;
        }
    }
}

std::size_t AsyncLoader::pendingCount() const noexcept {
    std::lock_guard<std::mutex> lk(impl_->qMu);
    return impl_->queue.size();
}

std::size_t AsyncLoader::inFlightCount() const noexcept {
    return impl_->inFlight.load(std::memory_order_relaxed);
}

std::size_t AsyncLoader::failedCount() const noexcept {
    return impl_->failed.load(std::memory_order_relaxed);
}

std::size_t AsyncLoader::workerCount() const noexcept {
    return impl_->workers.size();
}

} // namespace threadmaxx::assets
