// tou2d_hud_accessibility_test — M6.7 contract pin for the
// accessibility-driven HudSystem geometry.
//
// Pins:
//   * Default-state HP bar geometry — the foreground fill is drawn as
//     `kHpBarLines` stacked DebugLine segments; outline strokes frame
//     the bar; the background track contributes its own stack.
//   * hudScale 100% — fill end matches the documented base width
//     (`kBaseHpBarLengthWU = 56`); slot-color carries through.
//   * hudScale 50% / 150% — fill end scales linearly with
//     `hudScale / 100.0`.
//   * Low-HP threshold — at `hpFrac > kLowHpFracThreshold` the fill is
//     the slot color; at `hpFrac <= kLowHpFracThreshold` the fill is
//     pulsing red (R == 255, G == 0, B == 0, alpha < 255).
//   * Low-HP warning marker — a red-with-alpha-0xE0 DebugPoint appears
//     at top-center of the viewport when `hpFrac <= kLowHpFracThreshold`;
//     absent above the threshold. `bigWarnings == 1` doubles its
//     pixelSize.
//   * Photosensitive / screenShake flags round-trip through the
//     accessibility setter; they don't affect HUD geometry today.
//   * advancePulseForTest moves the cosmetic counter, producing a
//     different pulse alpha at the same low-HP state.

#include "Check.hpp"

#include "../examples/tou2d/CameraSystem.hpp"
#include "../examples/tou2d/DemoTypes.hpp"
#include "../examples/tou2d/HudSystem.hpp"
#include "../examples/tou2d/Settings.hpp"

#include <threadmaxx/render/RenderFrameBuilder.hpp>

#include <algorithm>
#include <cstdint>

namespace {

// 0xAABBGGRR: red lines = R=0xFF, G=0, B=0; green = G=0xFF; etc.
bool isPulseRed(std::uint32_t c) {
    return ((c      ) & 0xFFu) == 0xFFu &&
           ((c >>  8) & 0xFFu) == 0x00u &&
           ((c >> 16) & 0xFFu) == 0x00u;
}
bool isSlotGreen(std::uint32_t c) {
    return ((c      ) & 0xFFu) == 0x00u &&
           ((c >>  8) & 0xFFu) == 0xFFu &&
           ((c >> 16) & 0xFFu) == 0x00u;
}

// Max line length over all DebugLines matching predicate.
template <typename Pred>
float maxLineLen(const threadmaxx::RenderFrameBuilder& b, Pred pred) {
    float best = 0.0f;
    for (const auto& ln : b.debugLines()) {
        if (!pred(ln.colorRGBA)) continue;
        const float dx = ln.b.x - ln.a.x;
        if (dx > best) best = dx;
    }
    return best;
}

// Count lines whose length exceeds a threshold (filters out the short
// weapon-glyph segments — the HP bar is the only long horizontal).
template <typename Pred>
int countLongLines(const threadmaxx::RenderFrameBuilder& b, Pred pred,
                   float minLen) {
    int n = 0;
    for (const auto& ln : b.debugLines()) {
        if (!pred(ln.colorRGBA)) continue;
        const float dx = ln.b.x - ln.a.x;
        if (dx > minLen) ++n;
    }
    return n;
}

// Max alpha among lines matching predicate.
template <typename Pred>
std::uint32_t maxAlpha(const threadmaxx::RenderFrameBuilder& b, Pred pred) {
    std::uint32_t best = 0;
    for (const auto& ln : b.debugLines()) {
        if (!pred(ln.colorRGBA)) continue;
        const std::uint32_t al = (ln.colorRGBA >> 24) & 0xFFu;
        if (al > best) best = al;
    }
    return best;
}

} // namespace

int main() {
    using tou2d::AccessibilitySettings;
    using tou2d::CameraSystem;
    using tou2d::HudSystem;
    using tou2d::UserComponentIds;

    // Common scaffold: a real CameraSystem (default-constructed) gives
    // HudSystem `effectiveOrthoHalfH`, `viewportAspect`, and
    // `followCenter(0..3) == origin`. UserComponentIds default to
    // invalid; `update()` early-returns. Tests push slot state via the
    // M6.7 test hook and use slot 2 (green) so the slot-color filter
    // doesn't conflate with the low-HP pulse red.
    const UserComponentIds ids{};
    constexpr std::uint8_t kTestSlot = 2;  // green slot

    // ---- 1. Default state — full-HP, slot-color fill at base width -
    {
        CameraSystem cam(ids);
        cam.setNumHumans(4);  // include slot 2
        HudSystem hud(ids, &cam);

        hud.pushSlotStateForTest(kTestSlot, /*present=*/true, /*alive=*/true,
                                 /*hpFrac=*/1.0f, /*kills=*/0);

        threadmaxx::RenderFrameBuilder b;
        hud.buildRenderFrame(b);

        // The fill is the only "long" green line (glyphs are ≤ 7wu).
        // Three stacked at base width 56.
        const float fillLen = maxLineLen(b, isSlotGreen);
        CHECK(fillLen > 55.0f && fillLen < 57.0f);
        const int   fillCount = countLongLines(b, isSlotGreen, /*minLen=*/40.0f);
        CHECK_EQ(fillCount, 3);

        // No low-HP red fill at full HP.
        const float redFillLen = maxLineLen(b, isPulseRed);
        CHECK_EQ(static_cast<int>(redFillLen), 0);
    }

    // ---- 2. hudScale 150% — fill bar length grows 1.5× --------------
    {
        CameraSystem cam(ids);
        cam.setNumHumans(4);
        HudSystem hud(ids, &cam);
        AccessibilitySettings a{};
        a.hudScale = 150;
        hud.setAccessibility(a);

        hud.pushSlotStateForTest(kTestSlot, true, true, 1.0f, 0);

        threadmaxx::RenderFrameBuilder b;
        hud.buildRenderFrame(b);

        const float fillLen = maxLineLen(b, isSlotGreen);
        // 56 * 1.5 = 84.
        CHECK(fillLen > 83.0f && fillLen < 85.0f);
    }

    // ---- 3. hudScale 50% — fill bar length shrinks 0.5× ------------
    {
        CameraSystem cam(ids);
        cam.setNumHumans(4);
        HudSystem hud(ids, &cam);
        AccessibilitySettings a{};
        a.hudScale = 50;
        hud.setAccessibility(a);

        hud.pushSlotStateForTest(kTestSlot, true, true, 1.0f, 0);

        threadmaxx::RenderFrameBuilder b;
        hud.buildRenderFrame(b);

        const float fillLen = maxLineLen(b, isSlotGreen);
        // 56 * 0.5 = 28.
        CHECK(fillLen > 27.0f && fillLen < 29.0f);
    }

    // ---- 4. Low-HP fills with pulsing red, slot-green absent -------
    {
        CameraSystem cam(ids);
        cam.setNumHumans(4);
        HudSystem hud(ids, &cam);

        hud.pushSlotStateForTest(kTestSlot, true, true, 0.20f, 0);

        threadmaxx::RenderFrameBuilder b;
        hud.buildRenderFrame(b);

        // The fill is now pulse-red. Length = 56 * 0.2 = 11.2.
        const float redLen = maxLineLen(b, isPulseRed);
        CHECK(redLen > 11.0f && redLen < 11.5f);
        // 3 stacked red fill lines.
        const int redCount = countLongLines(b, isPulseRed, /*minLen=*/8.0f);
        CHECK_EQ(redCount, 3);
        // Pulse alpha is bounded; the maximum across the three stacked
        // lines (which share the same color) is below 255.
        const std::uint32_t maxA = maxAlpha(b, isPulseRed);
        CHECK(maxA < 255u);
        CHECK(maxA > 0u);

        // Green fill is GONE at low HP — pulse replaces it.
        const float greenLen = maxLineLen(b, isSlotGreen);
        // Glyph lines for the green slot are still emitted (≤ 7wu),
        // so greenLen should be small but non-zero.
        CHECK(greenLen < 10.0f);
    }

    // ---- 5. Warning marker fires below threshold + bigWarnings cap -
    {
        CameraSystem cam(ids);
        cam.setNumHumans(4);
        HudSystem hud(ids, &cam);

        // Above threshold → no warning point (filter on alpha 0xE0 +
        // red R=255 — the badge / score pips for green slot don't
        // match either condition).
        hud.pushSlotStateForTest(kTestSlot, true, true, 0.50f, 0);
        threadmaxx::RenderFrameBuilder b1;
        hud.buildRenderFrame(b1);
        bool foundWarn = false;
        for (const auto& p : b1.debugPoints()) {
            const std::uint32_t r = (p.colorRGBA      ) & 0xFFu;
            const std::uint32_t a = (p.colorRGBA >> 24) & 0xFFu;
            if (r == 0xFFu && a == 0xE0u) { foundWarn = true; break; }
        }
        CHECK(!foundWarn);

        // Below threshold + default bigWarnings = 0 → marker size = 8 px.
        hud.pushSlotStateForTest(kTestSlot, true, true, 0.10f, 0);
        threadmaxx::RenderFrameBuilder b2;
        hud.buildRenderFrame(b2);
        float warnSize = 0.0f;
        for (const auto& p : b2.debugPoints()) {
            const std::uint32_t r = (p.colorRGBA      ) & 0xFFu;
            const std::uint32_t a = (p.colorRGBA >> 24) & 0xFFu;
            if (r == 0xFFu && a == 0xE0u) {
                warnSize = std::max(warnSize, p.pixelSize);
            }
        }
        CHECK(warnSize > 7.5f && warnSize < 8.5f);

        // bigWarnings = 1 → marker size doubles to 16 px.
        AccessibilitySettings a{};
        a.bigWarnings = 1;
        hud.setAccessibility(a);
        threadmaxx::RenderFrameBuilder b3;
        hud.buildRenderFrame(b3);
        float bigWarnSize = 0.0f;
        for (const auto& p : b3.debugPoints()) {
            const std::uint32_t r = (p.colorRGBA      ) & 0xFFu;
            const std::uint32_t aa= (p.colorRGBA >> 24) & 0xFFu;
            if (r == 0xFFu && aa == 0xE0u) {
                bigWarnSize = std::max(bigWarnSize, p.pixelSize);
            }
        }
        CHECK(bigWarnSize > 15.5f && bigWarnSize < 16.5f);
    }

    // ---- 6. Accessibility round-trips ------------------------------
    {
        HudSystem hud(ids, nullptr);
        AccessibilitySettings a{};
        a.hudScale       = 175;
        a.bigWarnings    = 1;
        a.screenShake    = 0;
        a.photosensitive = 1;
        hud.setAccessibility(a);
        const auto got = hud.accessibility();
        CHECK_EQ(got.hudScale,       std::uint8_t{175});
        CHECK_EQ(got.bigWarnings,    std::uint8_t{1});
        CHECK_EQ(got.screenShake,    std::uint8_t{0});
        CHECK_EQ(got.photosensitive, std::uint8_t{1});
    }

    // ---- 7. Pulse counter advances render-side independent of step -
    {
        // Invoking advancePulseForTest changes the alpha produced by
        // the low-HP pulse — the sine oscillates over kPulsePeriod
        // ticks, so a quarter-cycle gap (5 ticks at period 20) is the
        // largest possible alpha swing. We expect a clearly different
        // alpha value across that gap.
        CameraSystem cam(ids);
        cam.setNumHumans(4);
        HudSystem hud(ids, &cam);
        hud.pushSlotStateForTest(kTestSlot, true, true, 0.10f, 0);

        auto pulseAlpha = [&]() -> std::uint32_t {
            threadmaxx::RenderFrameBuilder b;
            hud.buildRenderFrame(b);
            return maxAlpha(b, isPulseRed);
        };
        const std::uint32_t a1 = pulseAlpha();
        hud.advancePulseForTest(5);  // quarter cycle
        const std::uint32_t a2 = pulseAlpha();
        CHECK(a1 != a2);
    }

    EXIT_WITH_RESULT();
}
