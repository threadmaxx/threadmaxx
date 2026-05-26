#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/Handles.hpp>
#include <threadmaxx/System.hpp>

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace tou2d {

class BulletTerrainSystem;

/// Ship vs static terrain. Runs in its own wave AFTER MovementSystem so
/// it sees integrated positions and can push the ship out of solid
/// tiles by overwriting Transform + zeroing the offending velocity
/// axis.
///
/// Index design:
///   * preStep walks every chunk that carries the TerrainBlock user-
///     component bit and inserts (cellX, cellY) -> TileCell{handle, hp,
///     attr, chunkIndex, rowInChunk} into a hash map. Rebuilt on
///     `invalidate()` — fires when our crash-damage path destroys a
///     tile or the bullet-damage path does (host wires both).
///   * update samples a (2*range+1)² neighborhood around each ship's
///     grid cell and resolves any Solid overlap. On a contact whose
///     impact velocity exceeds `kCrashImpactSpeed`, both the ship and
///     the tile take a chip of damage (less than a weapon hit).
///
/// reads / writes:
///   * reads  = {Transform, Velocity}
///   * writes = {Transform, Velocity, EntityStructural}
///       — EntityStructural added in M3.2 because the crash-damage
///         path may destroy a tile entity inline.
class TerrainCollisionSystem : public threadmaxx::ISystem {
public:
    explicit TerrainCollisionSystem(UserComponentIds ids) noexcept;

    const char*              name() const noexcept override { return "tou2d.collision"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Velocity,
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Velocity,
            threadmaxx::Component::EntityStructural,
        };
    }

    void preStep(threadmaxx::SystemContext& ctx) override;
    void update(threadmaxx::SystemContext& ctx) override;

    /// Invalidate the cached grid — fires on tile destruction (our own
    /// crash-damage path or BulletTerrainSystem's bullet-damage path).
    void invalidate() noexcept { built_ = false; }

    /// Fires once per tile destroyed by the crash-damage path. Wired
    /// to the same host-side painter the bullet-damage path uses; both
    /// fire the same callback signature.
    using DestroyCallback =
        std::function<void(std::int32_t cellX, std::int32_t cellY)>;
    void setDestroyCallback(DestroyCallback cb) noexcept { destroyCb_ = std::move(cb); }

    /// Crash-damage path tells BulletTerrainSystem to drop its cached
    /// tile index on destruction. Borrowed; host installs after both
    /// systems exist.
    void setBulletTerrain(BulletTerrainSystem* bt) noexcept { bullets_ = bt; }

private:
    UserComponentIds        ids_;
    bool                    built_     = false;
    BulletTerrainSystem*    bullets_   = nullptr;   // borrowed
    DestroyCallback         destroyCb_;

    /// One entry per non-Air cell. `hp` mirrors the TerrainBlock value
    /// at last rebuild; the system mutates it in-place when the
    /// crash-damage path chips at it so the destroy-threshold check is
    /// consistent across consecutive ticks before the next rebuild.
    struct TileCell {
        threadmaxx::EntityHandle handle;
        std::uint8_t             hp;
        Attribute                attr;
        std::int16_t             cellX;
        std::int16_t             cellY;
    };
    std::unordered_map<std::int64_t, TileCell> grid_;

    static constexpr std::int64_t packCell(std::int32_t x, std::int32_t y) noexcept {
        return (static_cast<std::int64_t>(y) << 32) |
               (static_cast<std::int64_t>(static_cast<std::uint32_t>(x)));
    }
};

} // namespace tou2d
