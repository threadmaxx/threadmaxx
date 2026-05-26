#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

#include <cstdint>
#include <unordered_map>

namespace tou2d {

/// Ship vs static terrain. Runs in its own wave AFTER MovementSystem so
/// it sees integrated positions and can push the ship out of solid
/// tiles by overwriting Transform + zeroing the offending velocity
/// axis.
///
/// Index design:
///   * preStep walks every chunk that carries the TerrainBlock user-
///     component bit and inserts (cellX, cellY) -> Attribute into a
///     hash map. Only built once — the map is reused across ticks
///     until M3 introduces destruction (which will invalidate via a
///     TerrainChanged event).
///   * update samples a 3×3 neighborhood around each ship's grid cell
///     and resolves any Solid overlap. Damage / Air contribute no
///     push-out; Damage will mutate ship HP in M3.
///
/// reads / writes:
///   * reads  = {Transform, Velocity}
///   * writes = {Transform, Velocity}
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
        };
    }

    void preStep(threadmaxx::SystemContext& ctx) override;
    void update(threadmaxx::SystemContext& ctx) override;

    /// Invalidate the cached grid — M3 destruction will call this when
    /// any terrain block's attribute changes. Until then, M2 builds the
    /// grid once at first tick.
    void invalidate() noexcept { built_ = false; }

private:
    UserComponentIds ids_;
    bool             built_ = false;

    /// 64-bit packed (cellY << 32) | (cellX & 0xFFFFFFFF) → Attribute.
    /// Stable across map rehash, cheap to test for membership.
    std::unordered_map<std::int64_t, Attribute> grid_;

    static constexpr std::int64_t packCell(std::int32_t x, std::int32_t y) noexcept {
        return (static_cast<std::int64_t>(y) << 32) |
               (static_cast<std::int64_t>(static_cast<std::uint32_t>(x)));
    }
};

} // namespace tou2d
