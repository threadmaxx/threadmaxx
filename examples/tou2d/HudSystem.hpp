#pragma once

#include "DemoTypes.hpp"
#include "Settings.hpp"

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

    /// M6.7 — accessibility hookup. Caller forwards a copy of the
    /// current `Settings::accessibility` from UISystem; `hudScale`
    /// rescales every WU constant in `buildRenderFrame`, `bigWarnings`
    /// doubles the low-HP warning marker, `photosensitive` / `screenShake`
    /// stored for downstream lookups (only `bigWarnings` and `hudScale`
    /// affect HUD geometry today).
    void setAccessibility(AccessibilitySettings a) noexcept { access_ = a; }
    AccessibilitySettings accessibility() const noexcept { return access_; }

    /// @internal Test hook: inject a per-slot snapshot directly without
    /// stepping the engine. The accessibility test pushes synthesized
    /// `SlotState` values and inspects what `buildRenderFrame` emits.
    void pushSlotStateForTest(std::uint8_t slot, bool present, bool alive,
                              float hpFrac, std::uint32_t kills) noexcept;

    /// @internal Test hook: advance the cosmetic pulse counter without
    /// running `update()` (the low-HP red pulse drives off this tick).
    void advancePulseForTest(std::uint32_t ticks) noexcept;

    static constexpr std::uint32_t kMaxScorePips = 16;

    /// HP threshold below which the HUD pulses red + the warning marker
    /// fires. Exposed for tests.
    static constexpr float kLowHpFracThreshold = 0.25f;

private:
    struct SlotState {
        bool          present       = false; ///< LocalPlayer slot exists this tick
        bool          alive         = false; ///< not DisabledTag
        bool          permanentDead = false; ///< LSS-mode permanent death (respawnIn == sentinel)
        float         hpFrac        = 0.0f;  ///< currentHp / maxHp clamped [0, 1]
        std::uint32_t kills         = 0;
        std::uint16_t dumbfireAmmo  = 0;     ///< current magazine
        std::uint16_t dumbfireReload= 0;     ///< 0 = ready
        std::uint16_t specialAmmo   = 0;     ///< M5.6 — was spreadAmmo
        std::uint16_t specialReload = 0;     ///< M5.6 — was spreadReload
        std::uint8_t  specialKind   = 0;     ///< M5.6 — SpecialKind enum value
        std::uint8_t  _pad          = 0;
    };

    UserComponentIds              ids_;
    const CameraSystem*           camera_ = nullptr;
    std::array<SlotState, 4>      slots_{};
    std::shared_ptr<std::atomic<bool>> roundEnded_;
    const std::uint8_t*           winnerSlot_  = nullptr;
    const std::uint16_t*          winnerKills_ = nullptr;
    AccessibilitySettings         access_{};
    std::uint32_t                 pulseTick_ = 0;
};

} // namespace tou2d
