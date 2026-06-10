#include "threadmaxx_navmesh/obstacle.hpp"

#include "threadmaxx_navmesh/detail/spatial_hash.hpp"

#include <cstdint>
#include <unordered_map>

namespace threadmaxx::navmesh {

namespace {

/// Per-cell payload — keeps the obstacle's blocking AABB + areaMask
/// so the per-cell scan never has to round-trip through the source
/// table.
struct OverlayPayload {
    float minX{};
    float minZ{};
    float maxX{};
    float maxZ{};
    std::uint32_t areaMask{};
};

} // namespace

struct ObstacleOverlay::Impl {
    ObstacleOverlayConfig cfg;
    detail::SpatialHashXZ<ObstacleId, OverlayPayload> hash;
    /// The canonical record of every live obstacle. The spatial hash
    /// is a search structure built on top — `obstacles_` is the source
    /// of truth used by `update` to rebuild buckets correctly.
    std::unordered_map<ObstacleId, DynamicObstacle> obstacles;
    ObstacleId nextId{1};

    explicit Impl(ObstacleOverlayConfig c) : cfg(c), hash(c.cellSize) {}

    static OverlayPayload payloadFor(const DynamicObstacle& o) noexcept {
        OverlayPayload p;
        p.minX = o.center.x - o.halfExtents.x;
        p.maxX = o.center.x + o.halfExtents.x;
        p.minZ = o.center.z - o.halfExtents.z;
        p.maxZ = o.center.z + o.halfExtents.z;
        p.areaMask = o.areaMask;
        return p;
    }

    void insertInto(ObstacleId id, const DynamicObstacle& o) {
        const OverlayPayload p = payloadFor(o);
        hash.insertAabb(id, p, p.minX, p.minZ, p.maxX, p.maxZ);
    }
};

ObstacleOverlay::ObstacleOverlay(Config cfg)
    : impl_(std::make_unique<Impl>(cfg)) {}

ObstacleOverlay::~ObstacleOverlay() = default;
ObstacleOverlay::ObstacleOverlay(ObstacleOverlay&&) noexcept = default;
ObstacleOverlay& ObstacleOverlay::operator=(ObstacleOverlay&&) noexcept = default;

ObstacleId ObstacleOverlay::add(const DynamicObstacle& obstacle) {
    const ObstacleId id = impl_->nextId++;
    impl_->obstacles.emplace(id, obstacle);
    impl_->insertInto(id, obstacle);
    return id;
}

void ObstacleOverlay::update(ObstacleId id,
                             const DynamicObstacle& obstacle) {
    const auto it = impl_->obstacles.find(id);
    if (it == impl_->obstacles.end()) return;
    impl_->hash.removeKey(id);
    it->second = obstacle;
    impl_->insertInto(id, obstacle);
}

void ObstacleOverlay::remove(ObstacleId id) {
    const auto it = impl_->obstacles.find(id);
    if (it == impl_->obstacles.end()) return;
    impl_->hash.removeKey(id);
    impl_->obstacles.erase(it);
}

bool ObstacleOverlay::isBlocked(const Vec3& xz,
                                std::uint32_t callerMask) const noexcept {
    return impl_->hash.anyInCell(
        xz.x, xz.z, [&](const OverlayPayload& p) {
            if ((p.areaMask & callerMask) == 0u) return false;
            return xz.x >= p.minX && xz.x <= p.maxX &&
                   xz.z >= p.minZ && xz.z <= p.maxZ;
        });
}

std::size_t ObstacleOverlay::obstacleCount() const noexcept {
    return impl_->obstacles.size();
}

float ObstacleOverlay::cellSize() const noexcept {
    return impl_->cfg.cellSize;
}

} // namespace threadmaxx::navmesh
