#include "threadmaxx_assets/registry.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "threadmaxx_assets/loaders/bmfont.hpp"
#include "threadmaxx_assets/loaders/bmp.hpp"
#include "threadmaxx_assets/loaders/obj.hpp"
#include "threadmaxx_assets/loaders/ply.hpp"
#include "threadmaxx_assets/loaders/png.hpp"
#include "threadmaxx_assets/loaders/tga.hpp"
#include "threadmaxx_assets/loaders/wav.hpp"

namespace threadmaxx::assets {

namespace {

std::string canonicalize(std::string_view path) {
    if (path.empty()) return {};
    std::error_code ec;
    auto p = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
    if (ec) {
        return std::string(path);
    }
    return p.string();
}

} // namespace

struct AssetSlot {
    AssetId       id{kInvalidAssetId};
    AssetType     type{AssetType::Unknown};
    std::uint32_t generation{0};
    std::atomic<std::uint32_t> refCount{0};
    std::string   canonicalKey;
    bool          isFile{false};
    std::shared_ptr<void> data;     // dynamic_cast not used; type tag drives access
    bool          live{false};

    AssetSlot() = default;
    AssetSlot(AssetSlot&& other) noexcept
        : id(other.id),
          type(other.type),
          generation(other.generation),
          refCount(other.refCount.load(std::memory_order_relaxed)),
          canonicalKey(std::move(other.canonicalKey)),
          isFile(other.isFile),
          data(std::move(other.data)),
          live(other.live) {}
};

struct AssetRegistry::Impl {
    mutable std::shared_mutex                       mu;
    std::vector<std::unique_ptr<AssetSlot>>         slots;
    std::vector<std::uint32_t>                      freeList;
    std::unordered_map<std::string, AssetId>        keyToId;
    Stats                                           stats{};

    AssetSlot* getSlotChecked(AssetId id, std::uint32_t gen) const noexcept {
        if (id >= slots.size()) return nullptr;
        auto* s = slots[id].get();
        if (s == nullptr || !s->live || s->generation != gen) return nullptr;
        return s;
    }

    AssetId allocateSlot(std::unique_lock<std::shared_mutex>&) {
        if (!freeList.empty()) {
            const auto id = freeList.back();
            freeList.pop_back();
            return id;
        }
        const auto id = static_cast<AssetId>(slots.size());
        slots.push_back(std::make_unique<AssetSlot>());
        slots.back()->id = id;
        return id;
    }

    AssetId installSlot(std::unique_lock<std::shared_mutex>& lk,
                        AssetType type,
                        std::string canonicalKey,
                        bool isFile,
                        std::shared_ptr<void> data) {
        const auto id = allocateSlot(lk);
        auto* s = slots[id].get();
        s->type = type;
        ++s->generation;
        s->refCount.store(1, std::memory_order_relaxed);
        s->canonicalKey = canonicalKey;
        s->isFile = isFile;
        s->data = std::move(data);
        s->live = true;
        keyToId[std::move(canonicalKey)] = id;
        return id;
    }

    // Loads bytes from disk and turns them into the typed POD via the
    // requested loader. Returns shared_ptr<void> (typeless).
    template <class LoadFn>
    std::shared_ptr<void> tryDiskLoad(std::string_view path, LoadFn fn,
                                      bool& ok) {
        auto r = fn(path);
        if (!r.ok()) {
            ok = false;
            return {};
        }
        ok = true;
        using T = decltype(r.value);
        return std::shared_ptr<void>(
            new T(std::move(r.value)),
            [](void* p) { delete static_cast<T*>(p); });
    }
};

AssetRegistry::AssetRegistry() : impl_(std::make_unique<Impl>()) {}
AssetRegistry::~AssetRegistry() = default;

template <class T>
const T* AssetRegistry::tryGet(AssetId id, std::uint32_t gen) const noexcept {
    std::shared_lock<std::shared_mutex> lk(impl_->mu);
    auto* s = impl_->getSlotChecked(id, gen);
    if (s == nullptr) return nullptr;
    return static_cast<const T*>(s->data.get());
}

void AssetRegistry::retain(AssetId id, std::uint32_t gen) noexcept {
    std::shared_lock<std::shared_mutex> lk(impl_->mu);
    auto* s = impl_->getSlotChecked(id, gen);
    if (s == nullptr) return;
    s->refCount.fetch_add(1, std::memory_order_relaxed);
}

void AssetRegistry::release(AssetId id, std::uint32_t gen) noexcept {
    bool drop = false;
    {
        std::shared_lock<std::shared_mutex> lk(impl_->mu);
        auto* s = impl_->getSlotChecked(id, gen);
        if (s == nullptr) return;
        const auto prev = s->refCount.fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1) drop = true;
    }
    if (!drop) return;

    std::unique_lock<std::shared_mutex> lk(impl_->mu);
    if (id >= impl_->slots.size()) return;
    auto* s = impl_->slots[id].get();
    if (s == nullptr || !s->live || s->generation != gen) return;
    // Double-check after re-acquiring exclusive: another retain may have
    // raced in between.
    if (s->refCount.load(std::memory_order_acquire) != 0) return;

    impl_->keyToId.erase(s->canonicalKey);
    s->live = false;
    ++s->generation;
    s->canonicalKey.clear();
    s->data.reset();
    s->isFile = false;
    s->type = AssetType::Unknown;
    impl_->freeList.push_back(id);
    ++impl_->stats.evicted;
}

namespace {

template <class T, class LoadFn>
AssetHandle<T> loadAssetTemplated(AssetRegistry& self,
                                  AssetRegistry::Impl& impl,
                                  std::string_view path,
                                  AssetType type,
                                  LoadFn fn) {
    const auto key = canonicalize(path);
    if (key.empty()) {
        return AssetHandle<T>{};
    }

    {
        std::shared_lock<std::shared_mutex> rlk(impl.mu);
        const auto it = impl.keyToId.find(key);
        if (it != impl.keyToId.end()) {
            const auto id = it->second;
            if (id < impl.slots.size()) {
                auto* s = impl.slots[id].get();
                if (s != nullptr && s->live && s->type == type) {
                    s->refCount.fetch_add(1, std::memory_order_relaxed);
                    ++impl.stats.loadsDedup;
                    return AssetHandle<T>{&self, id, s->generation};
                }
            }
        }
    }

    auto r = fn(path);
    if (!r.ok()) {
        return AssetHandle<T>{};
    }
    auto data = std::shared_ptr<void>(
        new T(std::move(r.value)),
        [](void* p) { delete static_cast<T*>(p); });

    std::unique_lock<std::shared_mutex> wlk(impl.mu);
    // Re-check after acquiring write lock — another thread may have raced.
    const auto it = impl.keyToId.find(key);
    if (it != impl.keyToId.end()) {
        const auto id = it->second;
        auto* s = impl.slots[id].get();
        if (s != nullptr && s->live && s->type == type) {
            s->refCount.fetch_add(1, std::memory_order_relaxed);
            ++impl.stats.loadsDedup;
            return AssetHandle<T>{&self, id, s->generation};
        }
    }
    const auto id = impl.installSlot(wlk, type, key, /*isFile=*/true, std::move(data));
    ++impl.stats.loadsSync;
    return AssetHandle<T>{&self, id, impl.slots[id]->generation};
}

template <class T>
AssetHandle<T> addInjected(AssetRegistry& self,
                           AssetRegistry::Impl& impl,
                           std::string_view name,
                           AssetType type,
                           T value) {
    auto data = std::shared_ptr<void>(
        new T(std::move(value)),
        [](void* p) { delete static_cast<T*>(p); });
    std::unique_lock<std::shared_mutex> wlk(impl.mu);
    const std::string key{name};
    // If the canonical key is already alive, dedup against it (discards
    // the new value); covers the AsyncLoader pump-after-sync-load race.
    const auto it = impl.keyToId.find(key);
    if (it != impl.keyToId.end()) {
        const auto eid = it->second;
        auto* s = impl.slots[eid].get();
        if (s != nullptr && s->live && s->type == type) {
            s->refCount.fetch_add(1, std::memory_order_relaxed);
            ++impl.stats.loadsDedup;
            return AssetHandle<T>{&self, eid, s->generation};
        }
    }
    const auto id = impl.installSlot(wlk, type, key, /*isFile=*/false, std::move(data));
    ++impl.stats.loadsSync;
    return AssetHandle<T>{&self, id, impl.slots[id]->generation};
}

} // namespace

AssetHandle<MeshData> AssetRegistry::loadMesh(std::string_view path) {
    auto fn = [](std::string_view p) -> AssetResult<MeshData> {
        // Pick by extension; OBJ vs PLY.
        const auto ext = std::filesystem::path(p).extension().string();
        if (ext == ".ply" || ext == ".PLY") return loadPly(p);
        return loadObj(p);
    };
    return loadAssetTemplated<MeshData>(*this, *impl_, path, AssetType::Mesh, fn);
}

AssetHandle<TextureData> AssetRegistry::loadTexture(std::string_view path) {
    auto fn = [](std::string_view p) -> AssetResult<TextureData> {
        const auto ext = std::filesystem::path(p).extension().string();
        if (ext == ".bmp" || ext == ".BMP") return loadBmp(p);
        if (ext == ".tga" || ext == ".TGA") return loadTga(p);
        return loadPng(p);
    };
    return loadAssetTemplated<TextureData>(*this, *impl_, path, AssetType::Texture, fn);
}

AssetHandle<AudioClipData> AssetRegistry::loadAudio(std::string_view path) {
    return loadAssetTemplated<AudioClipData>(*this, *impl_, path,
                                             AssetType::Audio, loadWav);
}

AssetHandle<FontAtlas> AssetRegistry::loadFont(std::string_view path) {
    return loadAssetTemplated<FontAtlas>(*this, *impl_, path,
                                         AssetType::Font, loadBmfont);
}

AssetHandle<MeshData> AssetRegistry::addMesh(std::string_view name, MeshData v) {
    return addInjected<MeshData>(*this, *impl_, name, AssetType::Mesh, std::move(v));
}
AssetHandle<TextureData> AssetRegistry::addTexture(std::string_view name, TextureData v) {
    return addInjected<TextureData>(*this, *impl_, name, AssetType::Texture, std::move(v));
}
AssetHandle<AudioClipData> AssetRegistry::addAudio(std::string_view name, AudioClipData v) {
    return addInjected<AudioClipData>(*this, *impl_, name, AssetType::Audio, std::move(v));
}
AssetHandle<FontAtlas> AssetRegistry::addFont(std::string_view name, FontAtlas v) {
    return addInjected<FontAtlas>(*this, *impl_, name, AssetType::Font, std::move(v));
}

bool AssetRegistry::reload(AssetId id) {
    // Snapshot the slot key + type under read lock; do the (possibly slow)
    // file load outside any lock; install under write lock.
    std::string key;
    AssetType   type{AssetType::Unknown};
    bool isFile = false;
    {
        std::shared_lock<std::shared_mutex> lk(impl_->mu);
        if (id >= impl_->slots.size()) return false;
        auto* s = impl_->slots[id].get();
        if (s == nullptr || !s->live) return false;
        key   = s->canonicalKey;
        type  = s->type;
        isFile = s->isFile;
    }
    if (!isFile) return false;

    std::shared_ptr<void> newData;
    bool ok = false;
    switch (type) {
        case AssetType::Mesh: {
            const auto ext = std::filesystem::path(key).extension().string();
            auto r = (ext == ".ply" || ext == ".PLY") ? loadPly(key) : loadObj(key);
            if (r.ok()) {
                newData = std::shared_ptr<void>(
                    new MeshData(std::move(r.value)),
                    [](void* p) { delete static_cast<MeshData*>(p); });
                ok = true;
            }
            break;
        }
        case AssetType::Texture: {
            const auto ext = std::filesystem::path(key).extension().string();
            AssetResult<TextureData> r;
            if      (ext == ".bmp" || ext == ".BMP") r = loadBmp(key);
            else if (ext == ".tga" || ext == ".TGA") r = loadTga(key);
            else                                       r = loadPng(key);
            if (r.ok()) {
                newData = std::shared_ptr<void>(
                    new TextureData(std::move(r.value)),
                    [](void* p) { delete static_cast<TextureData*>(p); });
                ok = true;
            }
            break;
        }
        case AssetType::Audio: {
            auto r = loadWav(key);
            if (r.ok()) {
                newData = std::shared_ptr<void>(
                    new AudioClipData(std::move(r.value)),
                    [](void* p) { delete static_cast<AudioClipData*>(p); });
                ok = true;
            }
            break;
        }
        case AssetType::Font: {
            auto r = loadBmfont(key);
            if (r.ok()) {
                newData = std::shared_ptr<void>(
                    new FontAtlas(std::move(r.value)),
                    [](void* p) { delete static_cast<FontAtlas*>(p); });
                ok = true;
            }
            break;
        }
        default: break;
    }
    if (!ok) return false;

    std::unique_lock<std::shared_mutex> wlk(impl_->mu);
    if (id >= impl_->slots.size()) return false;
    auto* s = impl_->slots[id].get();
    if (s == nullptr || !s->live) return false;
    s->data = std::move(newData);
    ++impl_->stats.reloads;
    return true;
}

std::size_t AssetRegistry::liveAssetCount(AssetType t) const {
    std::shared_lock<std::shared_mutex> lk(impl_->mu);
    std::size_t n = 0;
    for (const auto& s : impl_->slots) {
        if (s != nullptr && s->live && (t == AssetType::Unknown || s->type == t)) {
            ++n;
        }
    }
    return n;
}

std::uint32_t AssetRegistry::refCount(AssetId id) const {
    std::shared_lock<std::shared_mutex> lk(impl_->mu);
    if (id >= impl_->slots.size()) return 0;
    auto* s = impl_->slots[id].get();
    if (s == nullptr || !s->live) return 0;
    return s->refCount.load(std::memory_order_relaxed);
}

AssetType AssetRegistry::typeOf(AssetId id) const {
    std::shared_lock<std::shared_mutex> lk(impl_->mu);
    if (id >= impl_->slots.size()) return AssetType::Unknown;
    auto* s = impl_->slots[id].get();
    if (s == nullptr || !s->live) return AssetType::Unknown;
    return s->type;
}

std::optional<std::string> AssetRegistry::pathOf(AssetId id) const {
    std::shared_lock<std::shared_mutex> lk(impl_->mu);
    if (id >= impl_->slots.size()) return std::nullopt;
    auto* s = impl_->slots[id].get();
    if (s == nullptr || !s->live) return std::nullopt;
    return s->canonicalKey;
}

std::vector<AssetRegistry::ResidentAsset>
AssetRegistry::listResident(AssetType filter) const {
    std::vector<ResidentAsset> out;
    std::shared_lock<std::shared_mutex> lk(impl_->mu);
    out.reserve(impl_->slots.size());
    for (std::size_t i = 0; i < impl_->slots.size(); ++i) {
        const auto& slot = impl_->slots[i];
        if (slot == nullptr || !slot->live) continue;
        if (filter != AssetType::Unknown && slot->type != filter) continue;
        ResidentAsset r{};
        r.id = static_cast<AssetId>(i);
        r.type = slot->type;
        r.refCount = slot->refCount.load(std::memory_order_relaxed);
        r.path = slot->canonicalKey;
        out.push_back(std::move(r));
    }
    return out;
}

namespace {

template <class T>
AssetHandle<T> findExistingTemplated(AssetRegistry& self,
                                     AssetRegistry::Impl& impl,
                                     std::string_view path,
                                     AssetType type) {
    const auto key = canonicalize(path);
    if (key.empty()) return AssetHandle<T>{};
    std::shared_lock<std::shared_mutex> rlk(impl.mu);
    const auto it = impl.keyToId.find(key);
    if (it == impl.keyToId.end()) return AssetHandle<T>{};
    const auto id = it->second;
    if (id >= impl.slots.size()) return AssetHandle<T>{};
    auto* s = impl.slots[id].get();
    if (s == nullptr || !s->live || s->type != type) return AssetHandle<T>{};
    s->refCount.fetch_add(1, std::memory_order_relaxed);
    return AssetHandle<T>{&self, id, s->generation};
}

} // namespace

AssetHandle<MeshData> AssetRegistry::findMesh(std::string_view path) {
    return findExistingTemplated<MeshData>(*this, *impl_, path, AssetType::Mesh);
}
AssetHandle<TextureData> AssetRegistry::findTexture(std::string_view path) {
    return findExistingTemplated<TextureData>(*this, *impl_, path, AssetType::Texture);
}
AssetHandle<AudioClipData> AssetRegistry::findAudio(std::string_view path) {
    return findExistingTemplated<AudioClipData>(*this, *impl_, path, AssetType::Audio);
}
AssetHandle<FontAtlas> AssetRegistry::findFont(std::string_view path) {
    return findExistingTemplated<FontAtlas>(*this, *impl_, path, AssetType::Font);
}

AssetRegistry::Stats AssetRegistry::stats() const {
    std::shared_lock<std::shared_mutex> lk(impl_->mu);
    return impl_->stats;
}

// Explicit instantiations for the templated tryGet.
template const MeshData*      AssetRegistry::tryGet<MeshData>     (AssetId, std::uint32_t) const noexcept;
template const TextureData*   AssetRegistry::tryGet<TextureData>  (AssetId, std::uint32_t) const noexcept;
template const AudioClipData* AssetRegistry::tryGet<AudioClipData>(AssetId, std::uint32_t) const noexcept;
template const FontAtlas*     AssetRegistry::tryGet<FontAtlas>    (AssetId, std::uint32_t) const noexcept;

} // namespace threadmaxx::assets
