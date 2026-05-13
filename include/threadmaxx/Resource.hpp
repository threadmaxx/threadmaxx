#pragma once

#include <cstdint>
#include <memory>
#include <typeindex>
#include <utility>

namespace threadmaxx {

/// Typed handle into a @ref ResourceRegistry.
///
/// The type tag is purely compile-time. At runtime the registry stores
/// resources type-erased and validates the type on every call via
/// `std::type_index`, so passing a `ResourceId<Mesh>` to
/// `get<Texture>(...)` returns nullptr even if the index happens to be
/// in range — the type check fails first.
///
/// `index/generation` mirrors @ref EntityHandle: stale IDs are detected
/// without searching, and removed slots are reused with bumped
/// generations.
template <typename T>
struct ResourceId {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    constexpr bool valid() const noexcept { return generation != 0; }
    constexpr bool operator==(const ResourceId&) const noexcept = default;
};

/// Engine-owned typed resource store.
///
/// Thread-safe under a single internal mutex — fine for setup-time
/// registration and per-frame lookups, not designed for high-throughput
/// concurrent inserts (an async loader should do I/O off-thread and
/// call @ref add once each resource is ready).
///
/// Lifetime: the registry owns each stored value via
/// `std::shared_ptr<void>`; @ref remove drops the registry's reference
/// and the value is destroyed when the last outstanding shared_ptr is
/// gone.
///
/// @thread_safety All public methods are safe from any thread, including
///                worker jobs.
class ResourceRegistry {
public:
    ResourceRegistry();
    ~ResourceRegistry();

    ResourceRegistry(const ResourceRegistry&) = delete;
    ResourceRegistry& operator=(const ResourceRegistry&) = delete;

    /// Move-construct `value` into the registry.
    /// @return A handle that validates against future get()/remove()
    ///         calls. Newly issued handles always have `generation != 0`.
    template <typename T>
    ResourceId<T> add(T value) {
        auto holder = std::shared_ptr<void>(
            new T(std::move(value)),
            [](void* p) noexcept { delete static_cast<T*>(p); });
        const auto raw = addRaw_(std::type_index(typeid(T)), std::move(holder));
        return ResourceId<T>{raw.first, raw.second};
    }

    /// @return Pointer to the stored value, or nullptr if the handle is
    ///         stale or refers to a different type than the one given at
    ///         add()-time.
    template <typename T>
    const T* get(ResourceId<T> id) const noexcept {
        const void* p = getRaw_(std::type_index(typeid(T)), id.index, id.generation);
        return static_cast<const T*>(p);
    }

    /// Drops the registry's ownership of the resource. The slot is
    /// reusable; subsequent adds may reuse the index with a bumped
    /// generation.
    /// @return true iff the handle was live (and is now stale).
    template <typename T>
    bool remove(ResourceId<T> id) noexcept {
        return removeRaw_(std::type_index(typeid(T)), id.index, id.generation);
    }

    /// Number of live resources of type T currently in the registry.
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
