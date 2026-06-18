// tou2d_water_splash_test — N3 (2026-06-18) water-splash contract.
//
// Pins:
//   * `audio::kSoundWaterSplash` enum value (5) and the bumped
//     `audio::kSoundCount` (6). Stable IDs so settings.dat / replay
//     files referencing audio slots don't shift under future audio
//     additions.
//   * `ParticleSystem::emitWaterSplash` deposits droplets into the
//     pool (alive count goes up; size scales with intensity).
//   * `MovementSystem::kWetThrustThreshold` sits between Air and full
//     wetness so a half-submerged ship triggers the splash but a
//     dry-rooftop hover doesn't.
//
// The bullet-into-water destroy-on-impact and the wet-thrust audio
// emit aren't pinned here — they require a full TouGame setup. The
// kit-spawn test pattern from N2 covers that style if a regression
// shows up downstream.

#include "Check.hpp"

#include "../examples/tou2d/DemoTypes.hpp"
#include "../examples/tou2d/MovementSystem.hpp"
#include "../examples/tou2d/ParticleSystem.hpp"

int main() {
    using tou2d::ParticleSystem;
    using tou2d::MovementSystem;

    // ---- (1) audio enum + count ------------------------------------
    static_assert(tou2d::audio::kSoundWaterSplash == 5,
                  "kSoundWaterSplash must stay at id 5 — N3 stable slot "
                  "ID, kSoundCount bumps to 6 to match.");
    static_assert(tou2d::audio::kSoundCount == 6,
                  "kSoundCount bumped to 6 alongside kSoundWaterSplash.");

    // ---- (2) emitWaterSplash deposits + scales with intensity -------
    {
        ParticleSystem ps;
        CHECK_EQ(ps.aliveCount(), std::size_t{0});

        // Low intensity → small burst. Use 0.0f to hit the floor of 2
        // droplets (the `max(1, round(2 + 0))` = 2 path).
        ps.emitWaterSplash( 0.0f, 0.0f, 0.0f);
        const std::size_t lowCount = ps.aliveCount();
        CHECK(lowCount >= 1);   // at least the floor count

        // High intensity → bigger burst.
        ps.emitWaterSplash(10.0f, 0.0f, 1.0f);
        const std::size_t totalCount = ps.aliveCount();
        const std::size_t highCount  = totalCount - lowCount;
        CHECK(highCount >= lowCount);   // monotonically more at full intensity
    }

    // ---- (3) Wet-thrust threshold sits in a sensible band ----------
    static_assert(MovementSystem::kWetThrustThreshold > 0.0f,
                  "threshold > 0 so a totally dry ship doesn't splash");
    static_assert(MovementSystem::kWetThrustThreshold < 1.0f,
                  "threshold < 1 so a fully-submerged ship DOES splash");
    static_assert(MovementSystem::kWetSplashAudioInterval > 0,
                  "audio cadence is positive; 0 would emit every tick");

    EXIT_WITH_RESULT();
}
