#pragma once

#include "Settings.hpp"

#include <threadmaxx/System.hpp>

#include <array>
#include <cstdint>
#include <random>

namespace tou2d {

/// M5.3 — particle FX. Owns a fixed-size pool of `Particle` instances
/// and emits world-space debug points each tick. Three effect families:
///
///   * **Death explosion** — 16 ship-colored debris + 10 dark smoke
///     puffs at the dying ship's centroid. Replaces nothing; layered
///     OVER `ShipLifecycleSystem`'s 8-ray starburst.
///   * **Impact spark** — 5 short-lived bright sparks at the bullet/
///     ship contact point. Bullet kind tints color (dumbfire = warm
///     yellow, spread = orange).
///   * **Tile-break dust** — 6 dirt-colored debris at the broken cell's
///     world center; reuses the Debris motion profile (gravity-falling).
///
/// Particle pool is `kMaxParticles = 256` with a round-robin write head;
/// emissions past the pool size silently overwrite the oldest. Sized for
/// the heaviest realistic burst (4 simultaneous ship deaths = 4×26 = 104
/// + concurrent impact sparks + tile dust) with headroom.
///
/// Determinism: per-system mt19937 with fixed seed `0xFEEDBEEFu`. Same
/// emission stream → same render stream tick-for-tick.
///
/// reads / writes: none. The system never touches World storage; its
/// state is entirely private to the system instance. Wave-share with
/// any system is safe.
class ParticleSystem : public threadmaxx::ISystem {
public:
    ParticleSystem() noexcept = default;

    const char*              name()   const noexcept override { return "tou2d.particles"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return {}; }
    threadmaxx::ComponentSet writes() const noexcept override { return {}; }

    void update          (threadmaxx::SystemContext& ctx) override;
    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override;

    /// Spawn a death-explosion burst at world-space `(x, y)`. `slotColor`
    /// is the dying ship's palette tint (low 24 bits of RGBA; alpha
    /// derived from particle lifetime). Safe to call from inside a
    /// system's `ctx.single` body; not thread-safe across simultaneous
    /// emitters (rely on registration-order serialisation).
    void emitDeathExplosion(float x, float y, std::uint32_t slotColor);

    /// Spawn a bullet-impact spark burst at world-space `(x, y)`.
    /// `bulletColor` is the bullet kind's tint (see `kBulletColor*` in
    /// the .cpp).
    void emitImpactSpark   (float x, float y, std::uint32_t bulletColor);

    /// Spawn a tile-break dust burst at world-space `(x, y)` (the
    /// destroyed cell's centroid). Color is hardcoded dirt-brown — the
    /// terrain palette isn't reachable from the emitter call site and
    /// the rough-rock look only depends on the dust being noticeably
    /// darker than the surviving rock around it.
    void emitTileBreakDust (float x, float y);

    /// M7.3 §5.1 — engine thruster plume at world `(x, y)`. `(vx, vy)`
    /// is the particle's initial world velocity (typically `-forward *
    /// kThrustEjectSpeed` so the plume trails behind a moving ship).
    /// Color is interpolated from yellow-orange at spawn to red-orange
    /// at end-of-life via `thrustColorForAge` in `buildRenderFrame`;
    /// the stored `rgb` field is set to the spawn color so callers can
    /// inspect a freshly-spawned particle's color directly.
    void emitThrusterParticle(float x, float y, float vx, float vy);

    /// M7.3 §5.2 — damage smoke puff at world `(x, y)`. One puff per
    /// call; the caller (typically `ShipLifecycleSystem`) rate-limits
    /// via `damageSmokeInterval(hpFrac)` so puff cadence scales with
    /// damage. Color is hardcoded dark-gray (same family as the
    /// death-explosion smoke), TTL is shorter so the trail is closer
    /// to "wisps" than to a billowing cloud.
    void emitDamageSmoke(float x, float y);

    /// N3 (2026-06-18) — water splash at world `(x, y)`. `intensity` in
    /// [0.0, 1.0] scales the burst size: low intensity (thruster
    /// passing over a wet patch) emits ~2 droplets; full intensity
    /// (bullet impact) emits ~5. Color is cyan / pale-blue against the
    /// water-tile blue terrain so the splash reads against the
    /// background; vertical-biased initial velocity gives the splash
    /// its characteristic up-and-out shape. Reuses the `Debris` motion
    /// profile so droplets fall under gravity (no need for a new Kind).
    void emitWaterSplash(float x, float y, float intensity);

    /// M7.3 §5.1 — color for a thruster particle at remaining-life
    /// fraction `frac` (1.0 = just spawned, 0.0 = expired). Lerps
    /// from yellow-orange to red-orange in `0xAABBGGRR` packing.
    /// Static so tests can pin the curve without an Engine instance.
    static std::uint32_t thrustColorForAge(float frac) noexcept;

    /// M7.3 §5.2 — ticks-per-puff for a ship at HP fraction `hpFrac`.
    /// Returns 0 when no smoke should emit (`hpFrac >=
    /// kDamageSmokeFracThreshold`); otherwise a positive integer that
    /// monotonically decreases as `hpFrac` drops (more damage → more
    /// smoke). Static so tests can pin the rate curve.
    static std::uint32_t damageSmokeInterval(float hpFrac) noexcept;

    /// M7.3 §5.2 — HP fraction at which damage smoke begins. Below
    /// this threshold every ship leaks a periodic smoke puff whose
    /// cadence scales with how far below.
    static constexpr float kDamageSmokeFracThreshold = 0.4f;

    /// M7.3 §5.1 — thruster eject speed (world units / second). Used
    /// by callers to compute `(vx, vy) = -forward * kThrustEjectSpeed`
    /// before invoking `emitThrusterParticle`. Public so the test pin
    /// can recover deterministic spawn velocities.
    static constexpr float kThrustEjectSpeed = 90.0f;

    /// M7.3 §5.1 — spawn color (frac == 1.0). Yellow-orange, `0xAABBGGRR`.
    static constexpr std::uint32_t kThrustColorHot  = 0xFF40AAFFu;
    /// M7.3 §5.1 — end-of-life color (frac == 0.0). Red-orange, `0xAABBGGRR`.
    static constexpr std::uint32_t kThrustColorCool = 0xFF4040E0u;

    /// M6.7 — accessibility hookup. When `photosensitive == 1`, every
    /// particle's emit alpha is multiplied by `kPhotosensitiveAlphaScale`
    /// (= 0.4) in `buildRenderFrame` so the explosion / spark / dust
    /// flashes read as muted. `update()` is unaffected — the cap is
    /// strictly render-side, so determinism (commitHash, replay) is
    /// preserved.
    void setAccessibility(AccessibilitySettings a) noexcept { access_ = a; }
    AccessibilitySettings accessibility() const noexcept { return access_; }

    /// Photosensitive-mode alpha cap. Public so tests can pin the exact
    /// scale used.
    static constexpr float kPhotosensitiveAlphaScale = 0.4f;

    /// M6.9b — count of active particles in the round-robin pool
    /// (`ttlTicks > 0`). Scanned on demand; cheap (`kMaxParticles = 256`,
    /// ~256 ns at typical cache hit rates). Used by the F3 overlay.
    std::size_t aliveCount() const noexcept {
        std::size_t n = 0;
        for (const auto& p : pool_) {
            if (p.ttlTicks > 0) ++n;
        }
        return n;
    }

private:
    enum class Kind : std::uint8_t {
        Debris = 0,  ///< outward fly + falling gravity + fast fade
        Smoke  = 1,  ///< slower outward + anti-gravity (rises) + drag
        Spark  = 2,  ///< bright, no gravity, very short-lived
        Thrust = 3,  ///< M7.3 — engine plume; no gravity, color ages
    };

    /// Per-particle state. POD; trivially copyable for round-robin
    /// overwrite.
    struct Particle {
        float          x        = 0.0f;
        float          y        = 0.0f;
        float          vx       = 0.0f;
        float          vy       = 0.0f;
        std::uint32_t  rgb      = 0x00FFFFFFu;   ///< low 24 bits; alpha from ttl
        std::uint16_t  ttlTicks = 0;             ///< 0 = inactive
        std::uint16_t  maxTtl   = 1;
        float          pxSize   = 4.0f;          ///< initial pixel size; shrinks with ttl
        Kind           kind     = Kind::Debris;
    };

    static constexpr std::size_t kMaxParticles = 256;

    void spawn(const Particle& p);

    std::array<Particle, kMaxParticles> pool_{};
    std::uint32_t                       head_ = 0;
    std::mt19937                        rng_{0xFEEDBEEFu};
    AccessibilitySettings               access_{};
};

} // namespace tou2d
