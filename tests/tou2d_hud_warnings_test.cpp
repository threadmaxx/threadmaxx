// tou2d_hud_warnings_test — N5 (2026-06-18) HUD polish contract.
//
// Pins the §2.5 polish items shipped in N5:
//   * On-fire warning glyph appears at hpFrac in [0.25, 0.40] (between
//     the low-HP red zone and the on-fire threshold).
//   * Above kOnFireFracThreshold the warning glyph is absent.
//   * Below kLowHpFracThreshold the low-HP banner takes precedence;
//     the on-fire glyph is suppressed.
//   * Constants sit in a sensible band — kOnFireFracThreshold strictly
//     above kLowHpFracThreshold so the two markers never overlap.
//
// The damage-tick flash and low-ammo warning live behind the same
// pushSlotStateForTest hook, but the ammo fields aren't reachable from
// that helper today. Coverage there leans on the smoke binary.

#include "Check.hpp"

#include "../examples/tou2d/CameraSystem.hpp"
#include "../examples/tou2d/DemoTypes.hpp"
#include "../examples/tou2d/HudSystem.hpp"

#include <threadmaxx/render/RenderFrameBuilder.hpp>

#include <cstdint>

namespace {

// On-fire glyph: 0xC02060FFu — A=0xC0, R=0xFF (low byte), G=0x60, B=0x20.
// 0xAABBGGRR packing: byte0=R, byte1=G, byte2=B, byte3=A.
bool isOnFireOrange(std::uint32_t c) {
    return ((c      ) & 0xFFu) == 0xFFu &&
           ((c >>  8) & 0xFFu) == 0x60u &&
           ((c >> 16) & 0xFFu) == 0x20u;
}

int countDebugPoints(const threadmaxx::RenderFrameBuilder& b,
                     bool (*pred)(std::uint32_t)) {
    int n = 0;
    for (const auto& p : b.debugPoints()) {
        if (pred(p.colorRGBA)) ++n;
    }
    return n;
}

} // namespace

int main() {
    using tou2d::CameraSystem;
    using tou2d::HudSystem;
    using tou2d::UserComponentIds;

    const UserComponentIds ids{};
    constexpr std::uint8_t kTestSlot = 2;  // green slot — same as the other HUD tests

    // ---- (1) Threshold ordering — both constants in a sensible band -
    static_assert(HudSystem::kOnFireFracThreshold > HudSystem::kLowHpFracThreshold,
                  "On-fire glyph must fire ABOVE the low-HP banner zone "
                  "so the two warnings don't visually overlap.");
    static_assert(HudSystem::kOnFireFracThreshold < 1.0f,
                  "On-fire glyph at full HP would never go away.");

    // ---- (2) Above threshold — no on-fire glyph ---------------------
    {
        CameraSystem cam(ids);
        cam.setNumHumans(4);
        HudSystem hud(ids, &cam);
        hud.pushSlotStateForTest(kTestSlot, true, true, 0.80f, 0);

        threadmaxx::RenderFrameBuilder b;
        hud.buildRenderFrame(b);

        CHECK_EQ(countDebugPoints(b, isOnFireOrange), 0);
    }

    // ---- (3) In the warning band — exactly one on-fire glyph --------
    // hpFrac = 0.35 lies strictly between kLowHpFracThreshold (0.25)
    // and kOnFireFracThreshold (0.40); the on-fire glyph appears, the
    // low-HP banner stays silent.
    {
        CameraSystem cam(ids);
        cam.setNumHumans(4);
        HudSystem hud(ids, &cam);
        hud.pushSlotStateForTest(kTestSlot, true, true, 0.35f, 0);

        threadmaxx::RenderFrameBuilder b;
        hud.buildRenderFrame(b);

        CHECK_EQ(countDebugPoints(b, isOnFireOrange), 1);
    }

    // ---- (4) Below low-HP threshold — on-fire glyph suppressed -----
    // hpFrac = 0.10 — the low-HP banner owns the visual; on-fire glyph
    // is skipped to avoid visual noise (the steady red warning is
    // already screaming).
    {
        CameraSystem cam(ids);
        cam.setNumHumans(4);
        HudSystem hud(ids, &cam);
        hud.pushSlotStateForTest(kTestSlot, true, true, 0.10f, 0);

        threadmaxx::RenderFrameBuilder b;
        hud.buildRenderFrame(b);

        CHECK_EQ(countDebugPoints(b, isOnFireOrange), 0);
    }

    EXIT_WITH_RESULT();
}
