#pragma once

/// @file inspect.hpp
/// @brief Read-only summaries of engine state for editor panels.
///
/// Inspector caches no engine state across calls — every `listX()` is
/// a fresh snapshot. Cheap when called once per frame, but callers
/// should not invoke from a tight UI loop. No mutation; everything
/// editor-driven is funneled through `IEditCommand` (E3).

#include <cstdint>
#include <optional>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Handles.hpp>
#include <threadmaxx/Resource.hpp>

namespace threadmaxx::editor {

/// @brief One entity's editor-facing description.
struct EntitySummary {
    threadmaxx::EntityHandle handle{};
    std::string label;
    std::vector<std::string> components;
};

/// @brief One resource's editor-facing description. The editor's
/// resource panel renders one row per summary.
struct ResourceSummary {
    std::string name;
    std::string typeName;
    std::uint64_t refCount{0};
    bool stale{false};
};

/// @brief One system's editor-facing description.
struct SystemSummary {
    std::string name;
    std::uint32_t waveIndex{0};
    float lastStepMs{0.0f};
    std::uint32_t jobs{0};
};

/// @brief Wave-aware, resource-tracking inspection of a live Engine.
///
/// The engine's `ResourceRegistry` is type-keyed and does not enumerate
/// by name. The inspector exposes a per-type `trackResource<T>(id,
/// displayName)` opt-in: every tracked id becomes a row in
/// `listResources()`. Untracked resources are invisible to the panel —
/// game code is expected to declare what to surface.
class Inspector {
public:
    explicit Inspector(threadmaxx::Engine& engine) noexcept;

    std::vector<EntitySummary> listEntities() const;
    std::vector<ResourceSummary> listResources() const;
    std::vector<SystemSummary> listSystems() const;

    /// @brief Lookup a single entity by handle. Returns nullopt for a
    /// stale or never-spawned handle.
    std::optional<EntitySummary> entity(threadmaxx::EntityHandle handle) const;

    /// @brief Register a resource id for editor display.
    /// @param id          The id returned by `engine.resources().add(...)`.
    /// @param displayName Editor-side label (typically the asset path).
    /// @param typeName    Human-readable type — defaults to `typeid(T)`'s
    ///                    mangled name when omitted.
    template <typename T>
    void trackResource(threadmaxx::ResourceId<T> id,
                       std::string displayName,
                       std::string typeName = {}) {
        if (typeName.empty()) typeName = typeid(T).name();
        TrackedResource entry{
            std::type_index(typeid(T)),
            id.index, id.generation,
            std::move(displayName),
            std::move(typeName),
            &Inspector::refCountFor<T>,
        };
        tracked_.push_back(std::move(entry));
    }

    /// @brief Remove a tracked id (e.g. on asset unload).
    template <typename T>
    void untrackResource(threadmaxx::ResourceId<T> id) {
        untrackResourceRaw_(std::type_index(typeid(T)),
                            id.index, id.generation);
    }

    /// @brief Count of currently-tracked resource entries.
    std::size_t trackedResourceCount() const noexcept;

private:
    struct TrackedResource {
        std::type_index type;
        std::uint32_t index;
        std::uint32_t generation;
        std::string displayName;
        std::string typeName;
        // Type-erased refcount reader: takes the registry + ResourceId
        // wire pair, reconstructs `ResourceId<T>`, and returns the
        // current count.
        std::uint64_t (*refCountFn)(const threadmaxx::ResourceRegistry&,
                                    std::uint32_t, std::uint32_t);
    };

    void untrackResourceRaw_(std::type_index ti,
                             std::uint32_t index,
                             std::uint32_t generation);

    template <typename T>
    static std::uint64_t refCountFor(const threadmaxx::ResourceRegistry& reg,
                                     std::uint32_t index,
                                     std::uint32_t generation) {
        threadmaxx::ResourceId<T> id{index, generation};
        return static_cast<std::uint64_t>(reg.refCount(id));
    }

    threadmaxx::Engine* engine_;
    std::vector<TrackedResource> tracked_;
};

} // namespace threadmaxx::editor
