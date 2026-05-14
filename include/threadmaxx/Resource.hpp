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

class ResourceRegistry;

/// Refcounted RAII handle into a @ref ResourceRegistry slot (§3.2 batch 7).
///
/// Returned by @ref ResourceRegistry::addRefCounted and
/// @ref ResourceRegistry::acquire. While at least one ResourceHandle
/// points at a slot, the slot stays alive; when the last handle is
/// destroyed the entry is removed automatically.
///
/// The legacy `add`/`remove` path is independent: a slot created via
/// `add` is not refcount-managed, and `remove` still drops it
/// immediately regardless of any outstanding handles. Don't mix the two
/// patterns on the same slot.
///
/// Copyable (bumps the refcount) and movable (transfers ownership).
/// Default-constructed handles are null and safe to destroy.
///
/// @thread_safety Safe to copy / destroy from any thread; the registry's
///                internal mutex serializes the refcount adjustment.
template <typename T>
class ResourceHandle {
public:
    ResourceHandle() = default;

    ResourceHandle(const ResourceHandle& other) noexcept
        : registry_(other.registry_), id_(other.id_) { retain_(); }

    ResourceHandle(ResourceHandle&& other) noexcept
        : registry_(other.registry_), id_(other.id_) {
        other.registry_ = nullptr;
        other.id_ = ResourceId<T>{};
    }

    ResourceHandle& operator=(const ResourceHandle& other) noexcept {
        if (this == &other) return *this;
        release_();
        registry_ = other.registry_;
        id_       = other.id_;
        retain_();
        return *this;
    }

    ResourceHandle& operator=(ResourceHandle&& other) noexcept {
        if (this == &other) return *this;
        release_();
        registry_ = other.registry_;
        id_       = other.id_;
        other.registry_ = nullptr;
        other.id_       = ResourceId<T>{};
        return *this;
    }

    ~ResourceHandle() { release_(); }

    /// True iff the handle currently refers to a live slot.
    bool valid() const noexcept { return registry_ != nullptr && id_.valid(); }
    explicit operator bool() const noexcept { return valid(); }

    /// Underlying registry id (still valid even after the slot is freed,
    /// but `registry.get(id)` will return nullptr in that case).
    ResourceId<T> id() const noexcept { return id_; }

    /// Drop ownership without destroying the slot. Equivalent to moving
    /// into a temporary. Useful when handing the bare id off to legacy
    /// `add`-style callers.
    void reset() noexcept {
        release_();
        registry_ = nullptr;
        id_       = ResourceId<T>{};
    }

private:
    friend class ResourceRegistry;

    ResourceHandle(ResourceRegistry* reg, ResourceId<T> id) noexcept
        : registry_(reg), id_(id) {
        // The factory paths in ResourceRegistry create the handle with the
        // initial refcount already at 1, so we do NOT retain again here.
    }

    void retain_() noexcept;
    void release_() noexcept;

    ResourceRegistry* registry_ = nullptr;
    ResourceId<T>     id_{};
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

    /// Insert `value` and return a refcounted handle owning it (§3.2 batch 7).
    /// The slot is alive while at least one ResourceHandle copy survives;
    /// the last handle destruction removes the entry automatically. Pair
    /// with @ref acquire to share ownership across the codebase without
    /// passing the handle by value.
    template <typename T>
    ResourceHandle<T> addRefCounted(T value) {
        auto holder = std::shared_ptr<void>(
            new T(std::move(value)),
            [](void* p) noexcept { delete static_cast<T*>(p); });
        const auto raw = addRefCountedRaw_(std::type_index(typeid(T)),
                                           std::move(holder));
        return ResourceHandle<T>(this, ResourceId<T>{raw.first, raw.second});
    }

    /// Bump the refcount on an existing id (must have been created via
    /// @ref addRefCounted) and return a new owning handle. Returns a null
    /// handle if the id is stale or wasn't refcount-managed.
    template <typename T>
    ResourceHandle<T> acquire(ResourceId<T> id) noexcept {
        if (!id.valid()) return {};
        if (!retainRaw_(std::type_index(typeid(T)),
                        id.index, id.generation)) {
            return {};
        }
        return ResourceHandle<T>(this, id);
    }

    /// Diagnostic: current strong-reference count for `id`. Returns 0 for
    /// stale ids or ids that were added via the non-refcounted @ref add path.
    template <typename T>
    std::uint32_t refCount(ResourceId<T> id) const noexcept {
        if (!id.valid()) return 0;
        return refCountRaw_(std::type_index(typeid(T)),
                            id.index, id.generation);
    }

    /// @internal Used by ResourceHandle's destructor / copy.
    template <typename T>
    void retainHandleSlot(ResourceId<T> id) noexcept {
        if (!id.valid()) return;
        retainRaw_(std::type_index(typeid(T)), id.index, id.generation);
    }
    template <typename T>
    void releaseHandleSlot(ResourceId<T> id) noexcept {
        if (!id.valid()) return;
        releaseRaw_(std::type_index(typeid(T)), id.index, id.generation);
    }

private:
    std::pair<std::uint32_t, std::uint32_t> addRaw_(
        std::type_index, std::shared_ptr<void> value);
    std::pair<std::uint32_t, std::uint32_t> addRefCountedRaw_(
        std::type_index, std::shared_ptr<void> value);
    const void* getRaw_(std::type_index,
                        std::uint32_t index,
                        std::uint32_t generation) const noexcept;
    bool removeRaw_(std::type_index,
                    std::uint32_t index,
                    std::uint32_t generation) noexcept;
    bool retainRaw_(std::type_index,
                    std::uint32_t index,
                    std::uint32_t generation) noexcept;
    void releaseRaw_(std::type_index,
                     std::uint32_t index,
                     std::uint32_t generation) noexcept;
    std::uint32_t refCountRaw_(std::type_index,
                               std::uint32_t index,
                               std::uint32_t generation) const noexcept;
    std::size_t countRaw_(std::type_index) const noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

template <typename T>
inline void ResourceHandle<T>::retain_() noexcept {
    if (registry_) registry_->retainHandleSlot(id_);
}

template <typename T>
inline void ResourceHandle<T>::release_() noexcept {
    if (registry_) registry_->releaseHandleSlot(id_);
}

class Engine;

/// Bookkeeping a loader optionally surfaces to the engine and HUD
/// (§3.2 batch 7). All counters default to zero; loaders that don't
/// implement @ref IResourceLoader::stats just return the default.
struct LoaderStats {
    /// Items queued but not yet picked up by the I/O stage.
    std::uint64_t pendingLoads    = 0;
    /// Items currently in flight (read / decode / upload pipeline).
    std::uint64_t inFlight        = 0;
    /// Items ready to be claimed by `update()` and registered.
    std::uint64_t ready           = 0;
    /// Items that failed (parse errors, file-not-found, etc).
    std::uint64_t failed          = 0;
    /// §3.5 batch 12 — items cancelled by @ref IResourceLoader::cancel.
    /// Lifetime counter; loaders increment when a pending or in-flight
    /// request is dropped.
    std::uint64_t cancelled       = 0;
    /// Current resident byte count. Zero means "loader does not track".
    std::uint64_t memoryFootprint = 0;
    /// Configured ceiling. Zero means "no budget set / unbounded".
    std::uint64_t memoryBudget    = 0;
};

/// Optional sim-thread pump for asset I/O.
///
/// The engine never spawns threads for a loader; it pumps @ref update
/// once per tick at the end of `postStep`, on the simulation thread.
/// The implementation owns whatever async pool / file system / network
/// stack it needs and calls @ref ResourceRegistry::add when an asset
/// finishes loading. Cancelling, hot-reloading, prioritization, and
/// progress reporting are all loader-side concerns — keeping them off
/// the public surface keeps the engine renderer- and asset-format-
/// agnostic.
///
/// Register via `Engine::addResourceLoader`. The engine takes ownership;
/// loaders are torn down in reverse-registration order during
/// `Engine::shutdown` after @ref onShutdown has been invoked.
class IResourceLoader {
public:
    virtual ~IResourceLoader() = default;

    /// Called once per `Engine::step()`, after the last `postStep` hook
    /// commits, on the simulation thread. Cheap to call; the loader is
    /// expected to poll its own queues and call
    /// `engine.resources().add(...)` for any completed work.
    virtual void update(Engine& engine) = 0;

    /// Called once during `Engine::shutdown` before the loader is
    /// destroyed, in reverse-registration order. Use it to cancel any
    /// in-flight uploads, join I/O threads, or flush logs. The
    /// engine guarantees `update` is never called again after
    /// `onShutdown` returns.
    /// Default no-op; override if you have async work to finalize.
    virtual void onShutdown(Engine& /*engine*/) {}

    /// Notify the loader that a previously-installed asset is stale and
    /// should be reloaded. The default no-op covers loaders that don't
    /// participate in hot reload. Override to queue a reload; on the
    /// next `update` pump, install the new value via
    /// `engine.resources().add(...)` and emit an `AssetReloaded` event
    /// on the engine's `events<AssetReloaded>()` channel so subscribers
    /// can rewire to the new id.
    ///
    /// The id is passed type-erased as a `(index, generation,
    /// type_index)` triple so loaders can match without templating on
    /// `T`. Loaders that don't recognize the type ignore the call.
    virtual void markStale(std::uint32_t /*index*/,
                           std::uint32_t /*generation*/,
                           std::type_index /*type*/) {}

    /// Optional per-loader progress / memory accounting. Default returns
    /// a zeroed LoaderStats; loaders that maintain queues should
    /// override to surface them.
    /// @thread_safety The engine reads this on the sim thread between
    ///                ticks; concurrent calls from other threads must
    ///                be safe in the override.
    virtual LoaderStats stats() const noexcept { return {}; }

    /// §3.5 batch 12 — per-tick cancellation pump. The engine calls
    /// this on the simulation thread immediately BEFORE
    /// @ref update each tick. Loaders that maintain a queue of
    /// game-driven "cancel anything pointing at chunk X" requests
    /// process them here; the default is a no-op.
    ///
    /// Returns the number of items cancelled this call; the engine
    /// uses the return value only for instrumentation
    /// (`LoaderStats::cancelled` is the loader's responsibility to
    /// keep in sync).
    /// @thread_safety Sim thread only — same context as @ref update.
    virtual std::uint64_t cancel(Engine& /*engine*/) { return 0; }
};

/// Event published by the engine when a resource id is hot-reloaded
/// (§3.2 batch 7). Subscribe via `engine.events<AssetReloaded>()`.
/// The `oldId` field is stale by the time the event reaches the
/// subscriber — use it only for comparing against cached ids that
/// game code is holding.
///
/// `oldId` / `newId` use raw `(index, generation)` pairs because the
/// channel is shared across all resource types; pair `type` with your
/// known `ResourceId<T>` to filter.
struct AssetReloaded {
    std::uint32_t oldIndex      = 0;
    std::uint32_t oldGeneration = 0;
    std::uint32_t newIndex      = 0;
    std::uint32_t newGeneration = 0;
    std::type_index type        = typeid(void);

    /// Helper: does this event refer to a specific typed id?
    template <typename T>
    bool matches(ResourceId<T> id) const noexcept {
        return type == std::type_index(typeid(T))
            && oldIndex == id.index
            && oldGeneration == id.generation;
    }
};

} // namespace threadmaxx
