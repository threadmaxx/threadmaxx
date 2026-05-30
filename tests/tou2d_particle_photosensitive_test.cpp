// tou2d_particle_photosensitive_test — M6.7 contract pin for the
// render-only photosensitive alpha cap on ParticleSystem.
//
// Pins:
//   * `setAccessibility` stores a copy of `AccessibilitySettings`;
//     `accessibility()` returns it untouched.
//   * `kPhotosensitiveAlphaScale == 0.4f` (pinned so a future tuning
//     change is a deliberate API event, not a silent drift).
//   * Default mode (photosensitive == 0) emits one DebugPoint per
//     active pool slot at full alpha (capped at frac-driven exponent;
//     unchanged from M5.3).
//   * Photosensitive mode (photosensitive == 1) multiplies each
//     emitted alpha by 0.4 — pair-wise comparison of identical pool
//     state with the cap on vs off shows every alpha is scaled
//     ~uniformly by the cap (within ±1 LSB rounding).
//   * The cap is strictly render-side: the pool itself is unchanged
//     (we re-render the same system twice without re-emitting; the
//     second render produces a different alpha histogram).

#include "Check.hpp"

#include "../examples/tou2d/ParticleSystem.hpp"
#include "../examples/tou2d/Settings.hpp"

#include <threadmaxx/render/RenderFrameBuilder.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

std::vector<std::uint32_t> alphasOf(const threadmaxx::RenderFrameBuilder& b) {
    std::vector<std::uint32_t> out;
    out.reserve(b.debugPoints().size());
    for (const auto& p : b.debugPoints()) {
        out.push_back((p.colorRGBA >> 24) & 0xFFu);
    }
    return out;
}

} // namespace

int main() {
    using tou2d::AccessibilitySettings;
    using tou2d::ParticleSystem;

    // ---- 1. kPhotosensitiveAlphaScale value pinned ------------------
    CHECK(ParticleSystem::kPhotosensitiveAlphaScale > 0.39f &&
          ParticleSystem::kPhotosensitiveAlphaScale < 0.41f);

    // ---- 2. setAccessibility round-trips ---------------------------
    {
        ParticleSystem ps;
        AccessibilitySettings a{};
        a.photosensitive = 1;
        a.bigWarnings    = 1;
        a.hudScale       = 125;
        a.screenShake    = 0;
        ps.setAccessibility(a);
        const auto got = ps.accessibility();
        CHECK_EQ(got.photosensitive, std::uint8_t{1});
        CHECK_EQ(got.bigWarnings,    std::uint8_t{1});
        CHECK_EQ(got.hudScale,       std::uint8_t{125});
        CHECK_EQ(got.screenShake,    std::uint8_t{0});
    }

    // ---- 3. Default mode — baseline alpha histogram -----------------
    std::vector<std::uint32_t> baseline;
    {
        ParticleSystem ps;
        // 16 debris + 10 smoke = 26 particles emitted.
        ps.emitDeathExplosion(100.0f, 50.0f, 0xFF00FF00u);
        threadmaxx::RenderFrameBuilder b;
        ps.buildRenderFrame(b);
        baseline = alphasOf(b);
        CHECK_EQ(baseline.size(), std::size_t{26});
        // At least some particles have non-trivial alpha (the
        // freshly-spawned ones).
        std::uint32_t maxA = 0;
        for (std::uint32_t v : baseline) maxA = std::max(maxA, v);
        CHECK(maxA > 100u);
    }

    // ---- 4. Photosensitive ON — uniform 0.4× scaling ---------------
    {
        ParticleSystem ps;
        AccessibilitySettings a{};
        a.photosensitive = 1;
        ps.setAccessibility(a);

        ps.emitDeathExplosion(100.0f, 50.0f, 0xFF00FF00u);
        threadmaxx::RenderFrameBuilder b;
        ps.buildRenderFrame(b);
        const auto capped = alphasOf(b);
        CHECK_EQ(capped.size(), std::size_t{26});

        // For each particle, the capped alpha is ~ baseline * 0.4
        // (within ±1 LSB from clamp + integer cast). The particle pool
        // is seeded by a per-system deterministic mt19937 with fixed
        // seed `0xFEEDBEEFu`, so the two emissions produce the same
        // ttl values in the same order → 1:1 comparison is valid.
        for (std::size_t i = 0; i < capped.size(); ++i) {
            const float expected = static_cast<float>(baseline[i]) *
                                   ParticleSystem::kPhotosensitiveAlphaScale;
            const float diff = static_cast<float>(capped[i]) - expected;
            CHECK(diff > -1.5f && diff < 1.5f);
        }

        // At least one capped alpha must be strictly less than the
        // matching baseline (proves the cap is doing something — when
        // baseline == 0 the cap is a no-op, but the freshly-spawned
        // particles have nonzero alpha).
        bool foundReduction = false;
        for (std::size_t i = 0; i < capped.size(); ++i) {
            if (baseline[i] > 5u && capped[i] < baseline[i]) {
                foundReduction = true;
                break;
            }
        }
        CHECK(foundReduction);
    }

    // ---- 5. Cap is render-side only — flipping it mid-life updates -
    {
        // Emit once; render in default mode; flip the cap on; render
        // again WITHOUT re-emitting. Pool state is preserved.
        ParticleSystem ps;
        ps.emitDeathExplosion(100.0f, 50.0f, 0xFF00FF00u);

        threadmaxx::RenderFrameBuilder b1;
        ps.buildRenderFrame(b1);
        const auto withCapOff = alphasOf(b1);

        AccessibilitySettings a{};
        a.photosensitive = 1;
        ps.setAccessibility(a);

        threadmaxx::RenderFrameBuilder b2;
        ps.buildRenderFrame(b2);
        const auto withCapOn = alphasOf(b2);

        CHECK_EQ(withCapOff.size(), withCapOn.size());
        bool seenDifference = false;
        for (std::size_t i = 0; i < withCapOff.size(); ++i) {
            if (withCapOff[i] != withCapOn[i]) seenDifference = true;
        }
        CHECK(seenDifference);
    }

    EXIT_WITH_RESULT();
}
