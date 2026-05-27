#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace tou2d {

class CameraSystem;

/// M3.4 — per-slot score + HP HUD rendered as debug geometry.
///
/// The Vulkan renderer consumes `DebugLine` / `DebugPoint` but NOT
/// `DebugText`, so the HUD is built out of those primitives. For each
/// of the 4 LocalPlayer slots we draw a slot-colored "badge" point
/// in one corner of the camera view, a horizontal row of small points
/// representing score (one per kill, saturating at `kMaxScorePips`),
/// and an HP bar made of one debug-line segment whose length tracks
/// `currentHp / maxHp`.
///
/// `update()` latches per-slot state (alive / hp frac / score) into
/// `slots_`. `buildRenderFrame()` runs after every postStep on the
/// sim thread and emits the geometry — it reads `slots_` plus the
/// camera's current follow center / extents (borrowed CameraSystem*).
///
/// reads()  = Transform + UserData (Ship + LocalPlayer).
/// writes() = none.
class HudSystem : public threadmaxx::ISystem {
public:
    HudSystem(UserComponentIds ids, const CameraSystem* camera) noexcept;

    const char*              name()   const noexcept override { return "tou2d.hud"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::UserData,
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }

    void update          (threadmaxx::SystemContext& ctx) override;
    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override;

    /// M4.2 — round-end shared latch + winner pointers (borrowed from
    /// TouGame). When the latch is set, buildRenderFrame draws a
    /// centered winner banner above the normal corner HUDs.
    void setRoundEndedFlag(std::shared_ptr<std::atomic<bool>> f,
                           const std::uint8_t*  winnerSlot,
                           const std::uint16_t* winnerKills) noexcept {
        roundEnded_  = std::move(f);
        winnerSlot_  = winnerSlot;
        winnerKills_ = winnerKills;
    }

    static constexpr std::uint32_t kMaxScorePips = 16;

private:
    struct SlotState {
        bool          present       = false; ///< LocalPlayer slot exists this tick
        bool          alive         = false; ///< not DisabledTag
        float         hpFrac        = 0.0f;  ///< currentHp / maxHp clamped [0, 1]
        std::uint32_t kills         = 0;
        std::uint16_t dumbfireAmmo  = 0;     ///< current magazine
        std::uint16_t dumbfireReload= 0;     ///< 0 = ready
        std::uint16_t spreadAmmo    = 0;
        std::uint16_t spreadReload  = 0;
    };

    UserComponentIds              ids_;
    const CameraSystem*           camera_ = nullptr;
    std::array<SlotState, 4>      slots_{};
    std::shared_ptr<std::atomic<bool>> roundEnded_;
    const std::uint8_t*           winnerSlot_  = nullptr;
    const std::uint16_t*          winnerKills_ = nullptr;
};

} // namespace tou2d
