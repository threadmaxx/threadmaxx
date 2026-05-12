#include "threadmaxx/World.hpp"

#include "WorldImpl.hpp"

namespace threadmaxx {

World::World() : impl_ptr_(std::make_unique<internal::WorldImpl>(1024)) {}
World::~World() = default;

World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

const Transform* World::tryGetTransform(EntityHandle e) const noexcept {
    const auto i = impl_ptr_->storage.indexOf(e);
    if (i == std::numeric_limits<std::uint32_t>::max()) return nullptr;
    return &impl_ptr_->storage.transforms()[i];
}
const Velocity* World::tryGetVelocity(EntityHandle e) const noexcept {
    const auto i = impl_ptr_->storage.indexOf(e);
    if (i == std::numeric_limits<std::uint32_t>::max()) return nullptr;
    return &impl_ptr_->storage.velocities()[i];
}
const RenderTag* World::tryGetRenderTag(EntityHandle e) const noexcept {
    const auto i = impl_ptr_->storage.indexOf(e);
    if (i == std::numeric_limits<std::uint32_t>::max()) return nullptr;
    return &impl_ptr_->storage.renderTags()[i];
}
const UserData* World::tryGetUserData(EntityHandle e) const noexcept {
    const auto i = impl_ptr_->storage.indexOf(e);
    if (i == std::numeric_limits<std::uint32_t>::max()) return nullptr;
    return &impl_ptr_->storage.userData()[i];
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

std::size_t World::size() const noexcept {
    return impl_ptr_->storage.size();
}

} // namespace threadmaxx
