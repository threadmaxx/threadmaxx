#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/Handles.hpp>
#include <threadmaxx/System.hpp>

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace tou2d {

class TerrainCollisionSystem;

/// Bullet vs static terrain. Runs in its own wave AFTER
/// ProjectileSystem so it sees integrated bullet positions.
///
/// Each tick:
///   * preStep rebuilds a (cellX, cellY) → EntityHandle map walking
///     TerrainBlock chunks. The rebuild is gated on a dirty flag the
///     system flips itself whenever it destroys a tile, so the steady
///     state cost is one chunk scan + one map insert per Solid block
///     per tick where destruction happened.
///   * update walks every Bullet, classifies into a destination cell,
///     looks up the tile entity, decrements TerrainBlock.hp by the
///     bullet's `damage`. The bullet entity is always destroyed on
///     contact; the tile entity is destroyed only when its hp wraps
///     past zero (saturating sub on the std::uint8_t). 0xFF is
///     indestructible — bedrock from the synthetic arena.
///
/// `TerrainCollisionSystem` is invalidated alongside our own dirty
/// flag so the ship can fly into space the bullets just opened up.
///
/// reads / writes:
///   * reads  = {Transform}  (bullet positions)
///   * writes = {EntityStructural}  — destroys bullets + emptied tiles.
class BulletTerrainSystem : public threadmaxx::ISystem {
public:
    BulletTerrainSystem(UserComponentIds        ids,
                        TerrainCollisionSystem* collision) noexcept;

    const char*              name()   const noexcept override { return "tou2d.bulletTerrain"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::EntityStructural,
        };
    }

    void preStep(threadmaxx::SystemContext& ctx) override;
    void update(threadmaxx::SystemContext& ctx) override;

    /// Fires once per tile actually destroyed (HP wraps past zero).
    /// Arguments are the destroyed tile's world cell coordinates (the
    /// same values stored in `TerrainBlock::cellX` / `cellY`). Host
    /// code wires this to the background-bitmap painter.
    using DestroyCallback = std::function<void(std::int32_t cellX, std::int32_t cellY)>;
    void setDestroyCallback(DestroyCallback cb) noexcept { destroyCb_ = std::move(cb); }

private:
    UserComponentIds        ids_;
    TerrainCollisionSystem* collision_ = nullptr;   // borrowed
    bool                    dirty_     = true;
    DestroyCallback         destroyCb_;

    struct TileEntry {
        threadmaxx::EntityHandle handle;
        std::size_t              chunkIndex;
        std::size_t              rowInChunk;
    };
    /// (cellY << 32) | cellX → (terrain entity, chunk loc).
    std::unordered_map<std::int64_t, TileEntry> tileIndex_;

    static constexpr std::int64_t packCell(std::int32_t x, std::int32_t y) noexcept {
        return (static_cast<std::int64_t>(y) << 32) |
               (static_cast<std::int64_t>(static_cast<std::uint32_t>(x)));
    }
};

} // namespace tou2d
