#pragma once

#include <cstdint>
#include <memory>
#include <typeindex>
#include <utility>

namespace threadmaxx {

// Typed handle into a ResourceRegistry. The type tag is purely compile-time
// — at runtime the registry stores resources type-erased and validates the
// type on every call via std::type_index.
//
//     ResourceId<Mesh> id = engine.resources().add(loadMesh("oak.gltf"));
//     if (const Mesh* m = engine.resources().get(id)) { /* use m */ }
//
// Index/generation form mirrors EntityHandle: stale IDs are detected
// without searching, and removed slots are reused with bumped generations.
template <typename T>
struct ResourceId {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    constexpr bool valid() const noexcept { return generation != 0; }
    constexpr bool operator==(const ResourceId&) const noexcept = default;
};

// Engine-owned typed resource store. Thread-safe under a single internal
// mutex — fine for setup-time registration and per-frame lookups, not
// designed for async loaders (those should publish through a higher-level
// channel and call add() once the resource is ready). Lifetime: the
// registry owns each stored value via shared_ptr<void>; remove() drops
// the registry's reference and the value is destroyed when the last
// outstanding shared_ptr is gone.
class ResourceRegistry {
public:
    ResourceRegistry();
    ~ResourceRegistry();

    ResourceRegistry(const ResourceRegistry&) = delete;
    ResourceRegistry& operator=(const ResourceRegistry&) = delete;

    // Move-construct value into the registry. Returns a handle that
    // validates against future get()/remove() calls.
    template <typename T>
    ResourceId<T> add(T value) {
        auto holder = std::shared_ptr<void>(
            new T(std::move(value)),
            [](void* p) noexcept { delete static_cast<T*>(p); });
        const auto raw = addRaw_(std::type_index(typeid(T)), std::move(holder));
        return ResourceId<T>{raw.first, raw.second};
    }

    // Returns nullptr if the handle is stale or refers to a different
    // type than the one given at add()-time.
    template <typename T>
    const T* get(ResourceId<T> id) const noexcept {
        const void* p = getRaw_(std::type_index(typeid(T)), id.index, id.generation);
        return static_cast<const T*>(p);
    }

    // Drops the registry's ownership of the resource. Returns true if the
    // handle was live (and is now stale).
    template <typename T>
    bool remove(ResourceId<T> id) noexcept {
        return removeRaw_(std::type_index(typeid(T)), id.index, id.generation);
    }

    // Number of live resources of type T currently in the registry.
    template <typename T>
    std::size_t count() const noexcept {
        return countRaw_(std::type_index(typeid(T)));
    }

private:
    std::pair<std::uint32_t, std::uint32_t> addRaw_(
        std::type_index, std::shared_ptr<void> value);
    const void* getRaw_(std::type_index,
                        std::uint32_t index,
                        std::uint32_t generation) const noexcept;
    bool removeRaw_(std::type_index,
                    std::uint32_t index,
                    std::uint32_t generation) noexcept;
    std::size_t countRaw_(std::type_index) const noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace threadmaxx
