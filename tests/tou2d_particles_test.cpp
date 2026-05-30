// tou2d_particles_test — pins the M7.3 ParticleSystem visual-polish
// contract.
//
// Contract pinned here:
//
//   (1) `ParticleSystem::thrustColorForAge(frac)` lerps from a
//       red-orange end (frac == 0) to a yellow-orange start (frac == 1)
//       in `0xAABBGGRR` packing. The endpoints exactly equal the
//       published `kThrustColorHot` / `kThrustColorCool` constants
//       (minus the alpha byte, which the caller ORs in separately).
//   (2) Each color channel (R, G, B) lerps monotonically across the
//       lifetime fraction — there's no overshoot in the middle and the
//       direction matches the visual intent (R cools from 255 → 224, B
//       warms from 64 → 64 [unchanged in this palette], G drops from
//       170 → 64).
//   (3) `ParticleSystem::damageSmokeInterval(hpFrac)` returns 0 at and
//       above `kDamageSmokeFracThreshold` (default 0.4), and a
//       positive integer that monotonically decreases as `hpFrac`
//       drops (more damage → more smoke). The endpoints land in the
//       documented band (~30 ticks/puff at threshold, ~3 at 0).
//   (4) End-to-end through `emitThrusterParticle` + `emitDamageSmoke`:
//       a freshly-emitted thrust particle is stored with the hot
//       color (low 24 bits) and `Kind::Thrust`; a damage-smoke puff
//       reuses `Kind::Smoke` and the dark-gray color band; both
//       persist through `buildRenderFrame` so a renderer sees them
//       as `DebugPoint` instances.

#include "Check.hpp"

#include "../examples/tou2d/ParticleSystem.hpp"

#include <threadmaxx/render/DebugGeometry.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>

#include <cstdint>

namespace {

constexpr std::uint32_t rgb24(std::uint32_t c) { return c & 0x00FFFFFFu; }

constexpr std::uint32_t chR(std::uint32_t c) { return (c >>  0) & 0xFFu; }
constexpr std::uint32_t chG(std::uint32_t c) { return (c >>  8) & 0xFFu; }
constexpr std::uint32_t chB(std::uint32_t c) { return (c >> 16) & 0xFFu; }

} // namespace

int main() {
    using tou2d::ParticleSystem;

    // ---- (1) Endpoints match the published constants -------------------
    {
        const std::uint32_t hot  = ParticleSystem::thrustColorForAge(1.0f);
        const std::uint32_t cool = ParticleSystem::thrustColorForAge(0.0f);
        CHECK_EQ(rgb24(hot),  rgb24(ParticleSystem::kThrustColorHot));
        CHECK_EQ(rgb24(cool), rgb24(ParticleSystem::kThrustColorCool));
    }

    // ---- (2) Per-channel monotonic lerp across age fractions -----------
    {
        // R cools from 255 (hot) to 224 (cool) — should decrease as frac
        // drops from 1 to 0; G drops sharply (170 → 64); B is the same
        // in both endpoints (64) so should be constant.
        const std::uint32_t cHot  = ParticleSystem::thrustColorForAge(1.0f);
        const std::uint32_t cMid  = ParticleSystem::thrustColorForAge(0.5f);
        const std::uint32_t cCool = ParticleSystem::thrustColorForAge(0.0f);

        CHECK(chR(cHot)  >= chR(cMid));
        CHECK(chR(cMid)  >= chR(cCool));
        CHECK(chG(cHot)  >= chG(cMid));
        CHECK(chG(cMid)  >= chG(cCool));
        CHECK_EQ(chB(cHot), chB(cCool));   // B unchanged in this palette

        // Sanity: midpoint is roughly the channel average (within ±2 LSBs).
        const auto near = [](std::uint32_t a, std::uint32_t b) {
            const int d = static_cast<int>(a) - static_cast<int>(b);
            return d <= 2 && d >= -2;
        };
        CHECK(near(chR(cMid), (chR(cHot) + chR(cCool)) / 2u));
        CHECK(near(chG(cMid), (chG(cHot) + chG(cCool)) / 2u));
    }

    // ---- (2b) Clamping at the boundaries -------------------------------
    {
        // Negative / over-1 fractions clamp to the endpoint colors.
        CHECK_EQ(rgb24(ParticleSystem::thrustColorForAge(-0.5f)),
                 rgb24(ParticleSystem::kThrustColorCool));
        CHECK_EQ(rgb24(ParticleSystem::thrustColorForAge( 1.5f)),
                 rgb24(ParticleSystem::kThrustColorHot));
    }

    // ---- (3) damageSmokeInterval: monotonic + threshold gate -----------
    {
        constexpr float thresh = ParticleSystem::kDamageSmokeFracThreshold;

        // Above (or at) threshold → no emission.
        CHECK_EQ(ParticleSystem::damageSmokeInterval(thresh),       0u);
        CHECK_EQ(ParticleSystem::damageSmokeInterval(thresh + 0.1f), 0u);
        CHECK_EQ(ParticleSystem::damageSmokeInterval(1.0f),          0u);

        // Below threshold → positive, monotonically non-increasing as
        // hpFrac drops (more damage → smaller interval = more smoke).
        std::uint32_t prev = ParticleSystem::damageSmokeInterval(thresh - 0.001f);
        CHECK(prev > 0u);
        for (int i = 0; i < 40; ++i) {
            const float hp = (static_cast<float>(40 - i - 1) / 40.0f) * thresh;
            const std::uint32_t now = ParticleSystem::damageSmokeInterval(hp);
            CHECK(now > 0u);
            CHECK(now <= prev);
            prev = now;
        }

        // Documented band: ~30 at threshold-minus-epsilon, ~3 at 0.
        const std::uint32_t atTopOfBand    = ParticleSystem::damageSmokeInterval(thresh - 0.001f);
        const std::uint32_t atBottomOfBand = ParticleSystem::damageSmokeInterval(0.0f);
        CHECK(atTopOfBand    >= 25u && atTopOfBand    <= 32u);
        CHECK(atBottomOfBand >= 1u  && atBottomOfBand <= 5u);
    }

    // ---- (4) End-to-end: emit + buildRenderFrame round-trip ------------
    {
        ParticleSystem ps;

        // One thrust puff. After buildRenderFrame, the first DebugPoint
        // should have RGB matching the hot start color (frac just below
        // 1 since ttlTicks < maxTtl after the first integrate; emit
        // happens here BEFORE update(), so we test directly after spawn).
        ps.emitThrusterParticle(/*x*/ 100.0f, /*y*/ 50.0f,
                                /*vx*/ -45.0f, /*vy*/ 0.0f);

        threadmaxx::RenderFrameBuilder b;
        ps.buildRenderFrame(b);
        const auto pts = b.debugPoints();
        CHECK_EQ(pts.size(), static_cast<std::size_t>(1));
        // Freshly-emitted Thrust particle: frac == 1, so RGB == hot.
        const auto pt = pts[0];
        CHECK_EQ(rgb24(pt.colorRGBA), rgb24(ParticleSystem::kThrustColorHot));
        CHECK(pt.position.x == 100.0f);
        CHECK(pt.position.y == 50.0f);
    }

    {
        // One damage-smoke puff. Reuses Kind::Smoke so the dark-gray
        // family applies; the buildRenderFrame keeps the stored rgb
        // for non-Thrust kinds, so we can pin the smoke palette byte
        // for byte against the documented dark-gray.
        ParticleSystem ps;
        ps.emitDamageSmoke(/*x*/ -25.0f, /*y*/ 10.0f);

        threadmaxx::RenderFrameBuilder b;
        ps.buildRenderFrame(b);
        const auto pts = b.debugPoints();
        CHECK_EQ(pts.size(), static_cast<std::size_t>(1));
        const auto pt = pts[0];
        CHECK_EQ(rgb24(pt.colorRGBA), 0x00606468u);
        CHECK(pt.position.x == -25.0f);
        CHECK(pt.position.y == 10.0f);
    }

    EXIT_WITH_RESULT();
}
