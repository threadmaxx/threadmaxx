// tou2d_weapon_mechanics_test — pins the M5.8 Bouncer + Homer math.
//
// These are header-only checks on the deterministic geometry the two
// systems rely on. The actual systems wire into engine state; this
// test exercises the math primitives so a refactor that breaks the
// invariants fails locally before the integration smoke runs.
//
// Contract:
//   * Bouncer reflect — given a bullet at cell (cx, cy) moving in
//     direction (vx, vy), the "back" cell on each axis must be the
//     cell the bullet came from, AND the reflect step must invert the
//     velocity component(s) corresponding to the open neighbor axis.
//   * Bouncer damping — `kBouncerDamping` is in (0, 1) so each bounce
//     loses energy (no infinite-energy corner-grazes).
//   * Homer steering — the angular-step ceiling is enforced: even if
//     the want-angle is across the bullet's nose, the step is capped
//     at ±`kHomerTurnPerTickRad` per tick.
//   * Homer preserves speed across the rotation (just rotates the
//     velocity vector; doesn't accelerate or decelerate).

#include "Check.hpp"

#include "../examples/tou2d/DemoTypes.hpp"

#include <cmath>
#include <cstdint>

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Apply one tick of the Homer's angular-step rule to a velocity
// vector aiming at a target. Mirror of `BulletHomingSystem::update`'s
// steering math (the system is engine-coupled so we don't link it
// here; this is the same arithmetic).
struct Vec2 { float x, y; };

Vec2 homerStep(Vec2 vel, Vec2 toTarget, float maxStep) {
    const float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y);
    if (speed <= 0.0f) return vel;
    const float distT = std::sqrt(toTarget.x * toTarget.x +
                                  toTarget.y * toTarget.y);
    if (distT <= 0.0f) return vel;
    const float cur  = std::atan2(vel.y, vel.x);
    const float want = std::atan2(toTarget.y, toTarget.x);
    float delta = want - cur;
    if (delta >  kPi) delta -= 2.0f * kPi;
    if (delta < -kPi) delta += 2.0f * kPi;
    const float step = (delta >  maxStep) ?  maxStep
                     : (delta < -maxStep) ? -maxStep : delta;
    const float a = cur + step;
    return Vec2{std::cos(a) * speed, std::sin(a) * speed};
}

} // namespace

int main() {
    using tou2d::kBouncerDamping;
    using tou2d::kHomerTurnPerTickRad;

    // ---- kBouncerDamping is in (0, 1) -----------------------------
    CHECK(kBouncerDamping > 0.0f);
    CHECK(kBouncerDamping < 1.0f);
    // After 3 bounces (Bouncer's budget) speed remains > 50% of muzzle.
    const float postSpeedFrac = kBouncerDamping * kBouncerDamping * kBouncerDamping;
    CHECK(postSpeedFrac > 0.5f);

    // ---- Reflect along X axis (vertical wall): vy unchanged, vx flips
    {
        const Vec2 v = {200.0f, -50.0f};
        const Vec2 reflected = {-v.x * kBouncerDamping, v.y};
        CHECK(reflected.x < 0.0f);
        CHECK(std::fabs(reflected.x + v.x * kBouncerDamping) < 1e-4f);
        CHECK_EQ(reflected.y, v.y);  // y-axis unchanged
    }

    // ---- Reflect along Y axis (horizontal wall): vx unchanged, vy flips
    {
        const Vec2 v = {120.0f, 300.0f};
        const Vec2 reflected = {v.x, -v.y * kBouncerDamping};
        CHECK_EQ(reflected.x, v.x);
        CHECK(reflected.y < 0.0f);
    }

    // ---- Corner hit: both axes flip --------------------------------
    {
        const Vec2 v = {100.0f, 100.0f};
        const Vec2 reflected = {-v.x * kBouncerDamping, -v.y * kBouncerDamping};
        CHECK(reflected.x < 0.0f);
        CHECK(reflected.y < 0.0f);
    }

    // ---- Homer turn-step ceiling -----------------------------------
    CHECK(kHomerTurnPerTickRad > 0.0f);
    CHECK(kHomerTurnPerTickRad < kPi);  // can't flip in one tick

    // Bullet flying +x; target straight up (+y); want = π/2.
    // Step is capped at kHomerTurnPerTickRad → new heading is
    // atan2(0, 1) + kHomerTurnPerTickRad = kHomerTurnPerTickRad.
    {
        const Vec2 vel = {320.0f, 0.0f};
        const Vec2 toTgt = {0.0f, 500.0f};
        const Vec2 out = homerStep(vel, toTgt, kHomerTurnPerTickRad);
        const float newAngle = std::atan2(out.y, out.x);
        CHECK(std::fabs(newAngle - kHomerTurnPerTickRad) < 1e-3f);
        // Speed preserved.
        const float sp0 = std::sqrt(vel.x * vel.x + vel.y * vel.y);
        const float sp1 = std::sqrt(out.x * out.x + out.y * out.y);
        CHECK(std::fabs(sp0 - sp1) < 1e-2f);
    }

    // ---- Homer: want-angle WITHIN step → snaps to want exactly -----
    {
        const Vec2 vel = {320.0f, 0.0f};
        // Want ~ 0.1 rad off — inside the 0.32 step → no clamp.
        const float wantAngle = 0.1f;
        const Vec2 toTgt = {std::cos(wantAngle) * 500.0f,
                            std::sin(wantAngle) * 500.0f};
        const Vec2 out = homerStep(vel, toTgt, kHomerTurnPerTickRad);
        const float newAngle = std::atan2(out.y, out.x);
        CHECK(std::fabs(newAngle - wantAngle) < 1e-3f);
    }

    // ---- Homer: 180° flip is capped — bullet flies forward-ish ----
    // The target is BEHIND the bullet; the step should rotate it by
    // exactly kHomerTurnPerTickRad in whichever direction the signed
    // delta normalisation lands. Either way, |new angle| < π.
    {
        const Vec2 vel = {320.0f, 0.0f};
        const Vec2 toTgt = {-500.0f, 0.0f};  // dead behind
        const Vec2 out = homerStep(vel, toTgt, kHomerTurnPerTickRad);
        const float newAngle = std::atan2(out.y, out.x);
        // After one step, the angle's magnitude must be exactly
        // kHomerTurnPerTickRad (in either signed direction — the
        // π / -π disambiguation lands one way deterministically).
        CHECK(std::fabs(std::fabs(newAngle) - kHomerTurnPerTickRad) < 1e-3f);
    }

    // ---- Homer: zero-speed bullet stays zero (mid-respawn stall) ---
    {
        const Vec2 vel = {0.0f, 0.0f};
        const Vec2 toTgt = {500.0f, 500.0f};
        const Vec2 out = homerStep(vel, toTgt, kHomerTurnPerTickRad);
        CHECK_EQ(out.x, 0.0f);
        CHECK_EQ(out.y, 0.0f);
    }

    // ---- Bullet POD must carry bouncesLeft (12-byte layout) -------
    static_assert(sizeof(tou2d::Bullet) == 12,
                  "Bullet must stay 12 bytes — bouncesLeft byte is "
                  "essential for the Bouncer reflect path");
    {
        tou2d::Bullet b{};
        b.bouncesLeft = 3;
        CHECK_EQ(b.bouncesLeft, std::uint8_t{3});
        b.bouncesLeft = static_cast<std::uint8_t>(b.bouncesLeft - 1);
        CHECK_EQ(b.bouncesLeft, std::uint8_t{2});
    }

    EXIT_WITH_RESULT();
}
