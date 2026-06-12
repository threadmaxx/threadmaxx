#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "data/audio.hpp"
#include "data/font.hpp"
#include "data/mesh.hpp"
#include "data/texture.hpp"
#include "types.hpp"

namespace threadmaxx::assets {

class AssetRegistry;

// Lightweight RAII handle. Holds a (registry, slot, generation) tuple
// plus a typed pointer cached from the most recent registry lookup.
template <class T>
class AssetHandle {
public:
    AssetHandle() noexcept = default;
    AssetHandle(AssetRegistry* reg, AssetId id, std::uint32_t gen) noexcept;
    AssetHandle(const AssetHandle& other) noexcept;
    AssetHandle(AssetHandle&& other) noexcept;
    AssetHandle& operator=(const AssetHandle& other) noexcept;
    AssetHandle& operator=(AssetHandle&& other) noexcept;
    ~AssetHandle();

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] AssetId id() const noexcept { return id_; }
    [[nodiscard]] const T* get() const noexcept;
    [[nodiscard]] const T& operator*() const noexcept { return *get(); }
    [[nodiscard]] const T* operator->() const noexcept { return get(); }

private:
    void retain() const noexcept;
    void release() noexcept;

    AssetRegistry* registry_{nullptr};
    AssetId        id_{kInvalidAssetId};
    std::uint32_t  generation_{0};
};

class AssetRegistry {
public:
    AssetRegistry();
    ~AssetRegistry();
    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;

    // Synchronous load + dedup. Identical canonical paths return handles
    // to the same slot. Failed loads return invalid handles.
    AssetHandle<MeshData>      loadMesh    (std::string_view path);
    AssetHandle<TextureData>   loadTexture (std::string_view path);
    AssetHandle<AudioClipData> loadAudio   (std::string_view path);
    AssetHandle<FontAtlas>     loadFont    (std::string_view path);

    // Inject pre-built data keyed by a logical name (instead of a file
    // path). Name namespace is shared with file paths.
    AssetHandle<MeshData>      addMesh   (std::string_view name, MeshData v);
    AssetHandle<TextureData>   addTexture(std::string_view name, TextureData v);
    AssetHandle<AudioClipData> addAudio  (std::string_view name, AudioClipData v);
    AssetHandle<FontAtlas>     addFont   (std::string_view name, FontAtlas v);

    // Force a reload from the original source path. Existing handles
    // stay valid; pointed-at content is replaced atomically.
    bool reload(AssetId id);

    // Existing-slot lookup (no disk I/O). Returns an invalid handle when
    // no slot matches. Used by AsyncLoader to dedup against in-flight or
    // already-installed records.
    AssetHandle<MeshData>      findMesh   (std::string_view path);
    AssetHandle<TextureData>   findTexture(std::string_view path);
    AssetHandle<AudioClipData> findAudio  (std::string_view path);
    AssetHandle<FontAtlas>     findFont   (std::string_view path);

    // Diagnostic accessors. Slot lookups are safe under concurrent reads.
    [[nodiscard]] std::size_t                  liveAssetCount(AssetType t = AssetType::Unknown) const;
    [[nodiscard]] std::uint32_t                refCount(AssetId id) const;
    [[nodiscard]] AssetType                    typeOf(AssetId id) const;
    [[nodiscard]] std::optional<std::string>   pathOf(AssetId id) const;

    struct Stats {
        std::uint64_t loadsSync{};
        std::uint64_t loadsDedup{};
        std::uint64_t reloads{};
        std::uint64_t evicted{};
    };
    [[nodiscard]] Stats stats() const;

    // Internal — used by AssetHandle.
    template <class T>
    [[nodiscard]] const T* tryGet(AssetId id, std::uint32_t gen) const noexcept;
    void retain(AssetId id, std::uint32_t gen) noexcept;
    void release(AssetId id, std::uint32_t gen) noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// AssetHandle inline definitions ------------------------------------------

template <class T>
inline AssetHandle<T>::AssetHandle(AssetRegistry* reg,
                                   AssetId id,
                                   std::uint32_t gen) noexcept
    : registry_(reg), id_(id), generation_(gen) {}

template <class T>
inline AssetHandle<T>::AssetHandle(const AssetHandle& other) noexcept
    : registry_(other.registry_),
      id_(other.id_),
      generation_(other.generation_) {
    retain();
}

template <class T>
inline AssetHandle<T>::AssetHandle(AssetHandle&& other) noexcept
    : registry_(other.registry_),
      id_(other.id_),
      generation_(other.generation_) {
    other.registry_   = nullptr;
    other.id_         = kInvalidAssetId;
    other.generation_ = 0;
}

template <class T>
inline AssetHandle<T>& AssetHandle<T>::operator=(const AssetHandle& other) noexcept {
    if (this != &other) {
        release();
        registry_   = other.registry_;
        id_         = other.id_;
        generation_ = other.generation_;
        retain();
    }
    return *this;
}

template <class T>
inline AssetHandle<T>& AssetHandle<T>::operator=(AssetHandle&& other) noexcept {
    if (this != &other) {
        release();
        registry_   = other.registry_;
        id_         = other.id_;
        generation_ = other.generation_;
        other.registry_   = nullptr;
        other.id_         = kInvalidAssetId;
        other.generation_ = 0;
    }
    return *this;
}

template <class T>
inline AssetHandle<T>::~AssetHandle() {
    release();
}

template <class T>
inline bool AssetHandle<T>::valid() const noexcept {
    return get() != nullptr;
}

template <class T>
inline const T* AssetHandle<T>::get() const noexcept {
    if (registry_ == nullptr || id_ == kInvalidAssetId) {
        return nullptr;
    }
    return registry_->template tryGet<T>(id_, generation_);
}

template <class T>
inline void AssetHandle<T>::retain() const noexcept {
    if (registry_ != nullptr && id_ != kInvalidAssetId) {
        registry_->retain(id_, generation_);
    }
}

template <class T>
inline void AssetHandle<T>::release() noexcept {
    if (registry_ != nullptr && id_ != kInvalidAssetId) {
        registry_->release(id_, generation_);
    }
    registry_   = nullptr;
    id_         = kInvalidAssetId;
    generation_ = 0;
}

} // namespace threadmaxx::assets
