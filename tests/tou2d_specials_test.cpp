// tou2d_specials_test — pins the M5.6 special-weapon catalogue.
//
// Contract:
//   * `SpecialKind` enum values 0..3 cover Spread / Rapid / Sniper /
//     Quintet, in stable order (must not be reordered — round-trips
//     through `WeaponLoadout.specialKind` and would invalidate any
//     persisted ship loadout).
//   * `kSpecialWeaponSpecs` has exactly `kSpecialKindCount` entries
//     and each is non-degenerate (positive magazine, positive speed,
//     positive ttl, bulletsPerShot >= 1).
//   * `weaponKind` bytes stamped into bullets are stable and unique
//     across kinds (so impact-spark routing in
//     BulletShipCollisionSystem stays distinct).
//   * `specialSpecAt(out-of-range)` clamps to entry 0 (Spread) without
//     UB, mirroring `shipKindAt` behaviour for forward-compat with
//     persisted snapshots that may carry stale kinds.

#include "Check.hpp"

#include "../examples/tou2d/DemoTypes.hpp"

#include <cstdint>
#include <set>

int main() {
    using tou2d::SpecialKind;
    using tou2d::kSpecialKindCount;
    using tou2d::kSpecialWeaponSpecs;
    using tou2d::specialSpecAt;

    // ---- Enum ordering pinned -------------------------------------------------
    CHECK_EQ(static_cast<std::uint8_t>(SpecialKind::Spread),  std::uint8_t{0});
    CHECK_EQ(static_cast<std::uint8_t>(SpecialKind::Rapid),   std::uint8_t{1});
    CHECK_EQ(static_cast<std::uint8_t>(SpecialKind::Sniper),  std::uint8_t{2});
    CHECK_EQ(static_cast<std::uint8_t>(SpecialKind::Quintet), std::uint8_t{3});
    CHECK_EQ(kSpecialKindCount, std::uint8_t{4});

    // ---- Each spec is non-degenerate -----------------------------------------
    std::set<std::uint8_t> seenWeaponKinds;
    for (std::uint8_t i = 0; i < kSpecialKindCount; ++i) {
        const auto& s = kSpecialWeaponSpecs[i];
        CHECK(s.magazine        > 0);
        CHECK(s.reloadTicks     > 0);
        CHECK(s.cooldownTicks   > 0);
        CHECK(s.bulletsPerShot >= 1);
        CHECK(s.muzzleSpeed     > 0.0f);
        CHECK(s.ttlSeconds      > 0.0f);
        CHECK(s.damagePerBullet > 0);
        seenWeaponKinds.insert(s.weaponKind);
    }
    // Every kind's stamped weaponKind is unique (no two kinds collide
    // on the impact-spark table).
    CHECK_EQ(seenWeaponKinds.size(), static_cast<std::size_t>(kSpecialKindCount));

    // ---- weaponKind values stable from DemoTypes.hpp comment table ----------
    // Spread = 1 (pre-M5.6 legacy), Rapid = 2, Sniper = 3, Quintet = 4.
    CHECK_EQ(kSpecialWeaponSpecs[0].weaponKind, std::uint8_t{1});
    CHECK_EQ(kSpecialWeaponSpecs[1].weaponKind, std::uint8_t{2});
    CHECK_EQ(kSpecialWeaponSpecs[2].weaponKind, std::uint8_t{3});
    CHECK_EQ(kSpecialWeaponSpecs[3].weaponKind, std::uint8_t{4});

    // ---- Shape checks: per-kind fan width / cadence reads as designed -------
    // Spread: 3 bullets in a fan.
    CHECK_EQ(kSpecialWeaponSpecs[0].bulletsPerShot, std::uint8_t{3});
    CHECK(kSpecialWeaponSpecs[0].spreadStepRad > 0.0f);
    // Rapid: 1 bullet, large mag.
    CHECK_EQ(kSpecialWeaponSpecs[1].bulletsPerShot, std::uint8_t{1});
    CHECK(kSpecialWeaponSpecs[1].magazine >= 8);
    // Sniper: 1 bullet, high single-shot damage, fast speed.
    CHECK_EQ(kSpecialWeaponSpecs[2].bulletsPerShot, std::uint8_t{1});
    CHECK(kSpecialWeaponSpecs[2].damagePerBullet >= 15);
    CHECK(kSpecialWeaponSpecs[2].muzzleSpeed >= 900.0f);
    // Quintet: 5 bullets in a tighter fan than Spread.
    CHECK_EQ(kSpecialWeaponSpecs[3].bulletsPerShot, std::uint8_t{5});
    CHECK(kSpecialWeaponSpecs[3].spreadStepRad > 0.0f);
    CHECK(kSpecialWeaponSpecs[3].spreadStepRad < kSpecialWeaponSpecs[0].spreadStepRad);

    // ---- Out-of-range clamp goes to Spread without UB ------------------------
    const auto& fallback = specialSpecAt(static_cast<std::uint8_t>(42));
    CHECK_EQ(fallback.weaponKind, kSpecialWeaponSpecs[0].weaponKind);
    CHECK_EQ(fallback.magazine,   kSpecialWeaponSpecs[0].magazine);

    // ---- WeaponLoadout still 16 bytes; specialKind round-trip ---------------
    static_assert(sizeof(tou2d::WeaponLoadout) == 16,
                  "WeaponLoadout must stay 16 bytes across M5.6 rename");
    tou2d::WeaponLoadout ld{};
    ld.specialKind = static_cast<std::uint8_t>(SpecialKind::Sniper);
    ld.specialAmmo = specialSpecAt(ld.specialKind).magazine;
    CHECK_EQ(ld.specialKind, std::uint8_t{2});
    CHECK_EQ(ld.specialAmmo, kSpecialWeaponSpecs[2].magazine);

    EXIT_WITH_RESULT();
}
