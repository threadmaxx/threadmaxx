#include "threadmaxx/World.hpp"

#include "WorldImpl.hpp"
#include "threadmaxx/Serialization.hpp"

#include <algorithm>

namespace threadmaxx {

World::World() : impl_ptr_(std::make_unique<internal::WorldImpl>(1024)) {}
World::~World() = default;

World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

namespace {

// Look up the chunk row for a handle without forcing a stitched-cache
// rebuild — needed by per-handle accessors that don't otherwise touch
// the legacy linear view.
template <typename T, typename ChunkAccessor>
const T* lookupChunkValue(const internal::WorldImpl& impl,
                          EntityHandle e, Component bit,
                          ChunkAccessor accessor) noexcept {
    const auto loc = impl.storage.locate(e);
    if (loc.archetype == std::numeric_limits<std::uint32_t>::max()) return nullptr;
    const auto& c = impl.storage.archetypes().chunks()[loc.archetype];
    if (!c.mask.has(bit)) return nullptr;
    return &accessor(c)[loc.row];
}

} // namespace

const Transform* World::tryGetTransform(EntityHandle e) const noexcept {
    return lookupChunkValue<Transform>(*impl_ptr_, e, Component::Transform,
        [](const internal::ArchetypeChunk& c) -> const auto& { return c.transforms; });
}
const Velocity* World::tryGetVelocity(EntityHandle e) const noexcept {
    return lookupChunkValue<Velocity>(*impl_ptr_, e, Component::Velocity,
        [](const internal::ArchetypeChunk& c) -> const auto& { return c.velocities; });
}
const RenderTag* World::tryGetRenderTag(EntityHandle e) const noexcept {
    return lookupChunkValue<RenderTag>(*impl_ptr_, e, Component::RenderTag,
        [](const internal::ArchetypeChunk& c) -> const auto& { return c.renderTags; });
}
const UserData* World::tryGetUserData(EntityHandle e) const noexcept {
    return lookupChunkValue<UserData>(*impl_ptr_, e, Component::UserData,
        [](const internal::ArchetypeChunk& c) -> const auto& { return c.userData; });
}
const Acceleration* World::tryGetAcceleration(EntityHandle e) const noexcept {
    return lookupChunkValue<Acceleration>(*impl_ptr_, e, Component::Acceleration,
        [](const internal::ArchetypeChunk& c) -> const auto& { return c.accelerations; });
}
const Parent* World::tryGetParent(EntityHandle e) const noexcept {
    return lookupChunkValue<Parent>(*impl_ptr_, e, Component::Parent,
        [](const internal::ArchetypeChunk& c) -> const auto& { return c.parents; });
}
const Health* World::tryGetHealth(EntityHandle e) const noexcept {
    return lookupChunkValue<Health>(*impl_ptr_, e, Component::Health,
        [](const internal::ArchetypeChunk& c) -> const auto& { return c.healths; });
}
const Faction* World::tryGetFaction(EntityHandle e) const noexcept {
    return lookupChunkValue<Faction>(*impl_ptr_, e, Component::Faction,
        [](const internal::ArchetypeChunk& c) -> const auto& { return c.factions; });
}
const AnimationStateRef* World::tryGetAnimationStateRef(EntityHandle e) const noexcept {
    return lookupChunkValue<AnimationStateRef>(*impl_ptr_, e, Component::AnimationStateRef,
        [](const internal::ArchetypeChunk& c) -> const auto& { return c.animationStates; });
}
const PhysicsBodyRef* World::tryGetPhysicsBodyRef(EntityHandle e) const noexcept {
    return lookupChunkValue<PhysicsBodyRef>(*impl_ptr_, e, Component::PhysicsBodyRef,
        [](const internal::ArchetypeChunk& c) -> const auto& { return c.physicsBodies; });
}
const NavAgentRef* World::tryGetNavAgentRef(EntityHandle e) const noexcept {
    return lookupChunkValue<NavAgentRef>(*impl_ptr_, e, Component::NavAgentRef,
        [](const internal::ArchetypeChunk& c) -> const auto& { return c.navAgents; });
}
const BoundingVolume* World::tryGetBoundingVolume(EntityHandle e) const noexcept {
    return lookupChunkValue<BoundingVolume>(*impl_ptr_, e, Component::BoundingVolume,
        [](const internal::ArchetypeChunk& c) -> const auto& { return c.boundingVolumes; });
}
const ComponentSet* World::tryGetComponentMask(EntityHandle e) const noexcept {
    return impl_ptr_->storage.tryGetComponentMask(e);
}

bool World::alive(EntityHandle e) const noexcept {
    return impl_ptr_->storage.alive(e);
}

std::span<const EntityHandle> World::entities() const noexcept {
    return {impl_ptr_->storage.entities().data(), impl_ptr_->storage.entities().size()};
}
std::span<const Transform> World::transforms() const noexcept {
    return {impl_ptr_->storage.transforms().data(), impl_ptr_->storage.transforms().size()};
}
std::span<const Velocity> World::velocities() const noexcept {
    return {impl_ptr_->storage.velocities().data(), impl_ptr_->storage.velocities().size()};
}
std::span<const RenderTag> World::renderTags() const noexcept {
    return {impl_ptr_->storage.renderTags().data(), impl_ptr_->storage.renderTags().size()};
}
std::span<const UserData> World::userData() const noexcept {
    return {impl_ptr_->storage.userData().data(), impl_ptr_->storage.userData().size()};
}
std::span<const Acceleration> World::accelerations() const noexcept {
    return {impl_ptr_->storage.accelerations().data(), impl_ptr_->storage.accelerations().size()};
}
std::span<const Parent> World::parents() const noexcept {
    return {impl_ptr_->storage.parents().data(), impl_ptr_->storage.parents().size()};
}
std::span<const Health> World::healths() const noexcept {
    return {impl_ptr_->storage.healths().data(), impl_ptr_->storage.healths().size()};
}
std::span<const Faction> World::factions() const noexcept {
    return {impl_ptr_->storage.factions().data(), impl_ptr_->storage.factions().size()};
}
std::span<const AnimationStateRef> World::animationStates() const noexcept {
    return {impl_ptr_->storage.animationStates().data(),
            impl_ptr_->storage.animationStates().size()};
}
std::span<const PhysicsBodyRef> World::physicsBodies() const noexcept {
    return {impl_ptr_->storage.physicsBodies().data(),
            impl_ptr_->storage.physicsBodies().size()};
}
std::span<const NavAgentRef> World::navAgents() const noexcept {
    return {impl_ptr_->storage.navAgents().data(),
            impl_ptr_->storage.navAgents().size()};
}
std::span<const BoundingVolume> World::boundingVolumes() const noexcept {
    return {impl_ptr_->storage.boundingVolumes().data(),
            impl_ptr_->storage.boundingVolumes().size()};
}
std::span<const ComponentSet> World::componentMasks() const noexcept {
    return {impl_ptr_->storage.componentMasks().data(), impl_ptr_->storage.componentMasks().size()};
}

std::size_t World::size() const noexcept {
    return impl_ptr_->storage.size();
}

std::vector<ArchetypeSignature> World::archetypeSignatures() const {
    // §3.1 batch 6: archetype-keyed storage means this is now O(num
    // archetypes) rather than O(num entities). Each chunk is one row.
    // Empty chunks are filtered out (the `ComponentSet::all()` chunk
    // is pre-allocated even in a never-populated world).
    const auto& chunks = impl_ptr_->storage.archetypes().chunks();
    std::vector<ArchetypeSignature> out;
    out.reserve(chunks.size());
    for (const auto& c : chunks) {
        if (c.entities.empty()) continue;
        out.push_back(ArchetypeSignature{
            c.mask, static_cast<std::uint32_t>(c.entities.size())});
    }
    std::sort(out.begin(), out.end(),
        [](const ArchetypeSignature& a, const ArchetypeSignature& b) {
            return a.mask.bits() < b.mask.bits();
        });
    return out;
}

std::size_t World::archetypeChunkCount() const noexcept {
    return impl_ptr_->storage.archetypes().chunks().size();
}

const internal::ArchetypeChunk& World::archetypeChunk(std::size_t i) const noexcept {
    return impl_ptr_->storage.archetypes().chunks()[i];
}

WorldSnapshot World::snapshot() const {
    const auto& s = impl_ptr_->storage;
    auto copySpan = [](auto& dst, auto src) {
        dst.assign(src.begin(), src.end());
    };
    WorldSnapshot out;
    copySpan(out.entities,        s.entities());
    copySpan(out.transforms,      s.transforms());
    copySpan(out.velocities,      s.velocities());
    copySpan(out.renderTags,      s.renderTags());
    copySpan(out.userData,        s.userData());
    copySpan(out.accelerations,   s.accelerations());
    copySpan(out.parents,         s.parents());
    copySpan(out.healths,         s.healths());
    copySpan(out.factions,        s.factions());
    copySpan(out.animationStates, s.animationStates());
    copySpan(out.physicsBodies,   s.physicsBodies());
    copySpan(out.navAgents,       s.navAgents());
    copySpan(out.boundingVolumes, s.boundingVolumes());
    copySpan(out.masks,           s.componentMasks());
    return out;
}

} // namespace threadmaxx
