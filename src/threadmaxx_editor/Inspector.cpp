/// @file Inspector.cpp
/// @brief Read-only summaries of engine state for editor panels.

#include "threadmaxx_editor/inspect.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx/Stats.hpp>
#include <threadmaxx/World.hpp>

#include <algorithm>
#include <cstring>

namespace threadmaxx::editor {

namespace {

struct BitName {
    threadmaxx::Component bit;
    const char* name;
};

constexpr BitName kComponentNames[] = {
    {threadmaxx::Component::Transform,         "Transform"},
    {threadmaxx::Component::Velocity,          "Velocity"},
    {threadmaxx::Component::RenderTag,         "RenderTag"},
    {threadmaxx::Component::UserData,          "UserData"},
    {threadmaxx::Component::Acceleration,      "Acceleration"},
    {threadmaxx::Component::Parent,            "Parent"},
    {threadmaxx::Component::Health,            "Health"},
    {threadmaxx::Component::Faction,           "Faction"},
    {threadmaxx::Component::AnimationStateRef, "AnimationStateRef"},
    {threadmaxx::Component::PhysicsBodyRef,    "PhysicsBodyRef"},
    {threadmaxx::Component::NavAgentRef,       "NavAgentRef"},
    {threadmaxx::Component::BoundingVolume,    "BoundingVolume"},
    {threadmaxx::Component::StaticTag,         "StaticTag"},
    {threadmaxx::Component::DisabledTag,       "DisabledTag"},
    {threadmaxx::Component::DestroyedTag,      "DestroyedTag"},
};

std::vector<std::string> componentNamesFor(threadmaxx::ComponentSet mask) {
    std::vector<std::string> out;
    out.reserve(4);
    for (const auto& e : kComponentNames) {
        if (mask.has(e.bit)) out.emplace_back(e.name);
    }
    return out;
}

EntitySummary makeSummary(const threadmaxx::World& world,
                          threadmaxx::EntityHandle h) {
    EntitySummary s;
    s.handle = h;
    s.label = std::string("entity#") + std::to_string(h.index);
    if (const threadmaxx::ComponentSet* mask =
            world.tryGetComponentMask(h)) {
        s.components = componentNamesFor(*mask);
    }
    return s;
}

} // namespace

Inspector::Inspector(threadmaxx::Engine& engine) noexcept
    : engine_(&engine) {}

std::vector<EntitySummary> Inspector::listEntities() const {
    const auto& world = engine_->world();
    const auto handles = world.entities();
    std::vector<EntitySummary> out;
    out.reserve(handles.size());
    for (auto h : handles) {
        out.push_back(makeSummary(world, h));
    }
    return out;
}

std::vector<ResourceSummary> Inspector::listResources() const {
    const auto& reg = engine_->resources();
    std::vector<ResourceSummary> out;
    out.reserve(tracked_.size());
    for (const auto& t : tracked_) {
        ResourceSummary s;
        s.name = t.displayName;
        s.typeName = t.typeName;
        s.refCount = t.refCountFn(reg, t.index, t.generation);
        s.stale = (s.refCount == 0);
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<SystemSummary> Inspector::listSystems() const {
    const auto stats = engine_->systemStats();
    const auto nodes = engine_->taskGraphSnapshot();
    std::vector<SystemSummary> out;
    out.reserve(stats.size());
    for (std::size_t i = 0; i < stats.size(); ++i) {
        SystemSummary s;
        s.name = stats[i].name ? stats[i].name : "";
        s.lastStepMs =
            static_cast<float>(stats[i].lastUpdateSeconds * 1000.0);
        s.jobs = static_cast<std::uint32_t>(stats[i].jobsSubmittedLastStep);
        if (i < nodes.size()) {
            s.waveIndex = static_cast<std::uint32_t>(nodes[i].wave);
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::optional<EntitySummary>
Inspector::entity(threadmaxx::EntityHandle handle) const {
    const auto& world = engine_->world();
    if (!world.alive(handle)) return std::nullopt;
    return makeSummary(world, handle);
}

void Inspector::untrackResourceRaw_(std::type_index ti,
                                    std::uint32_t index,
                                    std::uint32_t generation) {
    auto it = std::remove_if(tracked_.begin(), tracked_.end(),
        [&](const TrackedResource& t) {
            return t.type == ti && t.index == index &&
                   t.generation == generation;
        });
    tracked_.erase(it, tracked_.end());
}

std::size_t Inspector::trackedResourceCount() const noexcept {
    return tracked_.size();
}

} // namespace threadmaxx::editor
