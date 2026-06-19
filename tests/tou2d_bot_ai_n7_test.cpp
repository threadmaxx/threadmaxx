// tou2d_bot_ai_n7_test — N7 (2026-06-19) NPC AI overhaul contract.
//
// Pins the public surface added in N7:
//
//   * `BotDifficulty` enum has 4 presets + Count; values pinned for
//     persistence stability (Settings::gameplay.botDifficulty round-
//     trips through settings.dat).
//   * `botConfigForDifficulty` returns a `BotConfig` POD for each
//     preset; lookup with an out-of-range value falls back to Normal
//     so a corrupted byte can't break the AI.
//   * The `Normal` preset reproduces the legacy hardcoded constants
//     bit-for-bit (so pre-N7 replays / smoke tests stay equivalent).
//   * Monotonic difficulty curve — between Easy/Normal/Hard/Insane
//     the bot's aim *tightens*, fire arc *widens*, fire range *grows*,
//     and the chaos-fire chance *climbs*. Pin the direction, not the
//     exact values, so future tuning passes don't churn the test.
//   * `BotControlSystem::setDifficulty` writes through to `config()`;
//     `setConfig` lets host code override individual fields.
//
// The actual stuck-detection + reverse-thrust unstuck path requires a
// full world snapshot to exercise; it's pinned by the headless smoke
// binary (the regression mode this batch fixes was visible at
// playtest, not in CI).

#include "Check.hpp"

#include "../examples/tou2d/BotControlSystem.hpp"

#include <cstdint>

int main() {
    using tou2d::BotConfig;
    using tou2d::BotControlSystem;
    using tou2d::BotDifficulty;
    using tou2d::UserComponentIds;
    using tou2d::botConfigForDifficulty;
    using tou2d::kBotChaosFireChancePerTick;

    // ---- (1) Enum positions pinned for settings.dat stability -------
    static_assert(static_cast<std::uint8_t>(BotDifficulty::Easy)   == 0);
    static_assert(static_cast<std::uint8_t>(BotDifficulty::Normal) == 1);
    static_assert(static_cast<std::uint8_t>(BotDifficulty::Hard)   == 2);
    static_assert(static_cast<std::uint8_t>(BotDifficulty::Insane) == 3);
    static_assert(static_cast<std::uint8_t>(BotDifficulty::Count)  == 4);

    // ---- (2) Normal == legacy constants ----------------------------
    {
        const BotConfig& n = botConfigForDifficulty(BotDifficulty::Normal);
        // Legacy `kBotChaosFireChancePerTick` is exposed as the
        // pre-N7 baseline; the Normal preset has to match it for
        // replay / determinism parity.
        CHECK(n.chaosFireChance == kBotChaosFireChancePerTick);
        // Pre-N7 hardcoded fire range was 220 wu.
        CHECK(n.fireRange       == 220.0f);
        // Pre-N7 firing arc was ~10° (= 0.17 rad).
        CHECK(n.facingFireRad   == 0.17f);
    }

    // ---- (3) Out-of-range falls back to Normal ---------------------
    {
        const BotConfig& fallback =
            botConfigForDifficulty(static_cast<BotDifficulty>(255));
        const BotConfig& normal = botConfigForDifficulty(BotDifficulty::Normal);
        CHECK(fallback.chaosFireChance == normal.chaosFireChance);
        CHECK(fallback.fireRange       == normal.fireRange);
        CHECK(fallback.facingFireRad   == normal.facingFireRad);
    }

    // ---- (4) Monotonic difficulty curve ----------------------------
    {
        const auto& easy   = botConfigForDifficulty(BotDifficulty::Easy);
        const auto& normal = botConfigForDifficulty(BotDifficulty::Normal);
        const auto& hard   = botConfigForDifficulty(BotDifficulty::Hard);
        const auto& insane = botConfigForDifficulty(BotDifficulty::Insane);

        // Aim wobble TIGHTENS (decreases) as difficulty climbs.
        CHECK(easy.aimWobbleAmp   > normal.aimWobbleAmp);
        CHECK(normal.aimWobbleAmp > hard.aimWobbleAmp);
        CHECK(hard.aimWobbleAmp   > insane.aimWobbleAmp);

        // Fire arc WIDENS as difficulty climbs.
        CHECK(easy.facingFireRad   < normal.facingFireRad);
        CHECK(normal.facingFireRad < hard.facingFireRad);
        CHECK(hard.facingFireRad   < insane.facingFireRad);

        // Fire RANGE grows.
        CHECK(easy.fireRange   < normal.fireRange);
        CHECK(normal.fireRange < hard.fireRange);
        CHECK(hard.fireRange   < insane.fireRange);

        // Chaos fire grows.
        CHECK(easy.chaosFireChance   < normal.chaosFireChance);
        CHECK(normal.chaosFireChance < hard.chaosFireChance);
        CHECK(hard.chaosFireChance   < insane.chaosFireChance);

        // Special-weapon close chance grows.
        CHECK(easy.spreadChanceClose   < normal.spreadChanceClose);
        CHECK(normal.spreadChanceClose < hard.spreadChanceClose);
        CHECK(hard.spreadChanceClose   < insane.spreadChanceClose);
    }

    // ---- (5) Tunable bands stay sane -------------------------------
    {
        for (int d = 0; d < static_cast<int>(BotDifficulty::Count); ++d) {
            const auto& c = botConfigForDifficulty(static_cast<BotDifficulty>(d));
            CHECK(c.aimWobbleAmp      >= 0.0f);
            CHECK(c.aimWobbleAmp      <  1.0f);          // < 60° wobble
            CHECK(c.facingFireRad     >  0.0f);
            CHECK(c.facingFireRad     <  1.0f);
            CHECK(c.fireRange         >  0.0f);
            CHECK(c.fireRange         <  1000.0f);
            CHECK(c.chaosFireChance   >= 0.0f);
            CHECK(c.chaosFireChance   <  1.0f);
            CHECK(c.spreadChanceClose >= 0.0f);
            CHECK(c.spreadChanceClose <= 1.0f);
            CHECK(c.unstuckWindowTicks    > 0);
            CHECK(c.unstuckMinDispWU      > 0.0f);
            CHECK(c.unstuckCommitTicks    > 0);
        }
    }

    // ---- (6) setDifficulty writes through to config() ---------------
    {
        UserComponentIds ids{};
        BotControlSystem sys(ids);
        // Default is Normal.
        CHECK(sys.config().fireRange == 220.0f);

        sys.setDifficulty(BotDifficulty::Insane);
        CHECK(sys.config().fireRange       == 300.0f);
        CHECK(sys.config().aimWobbleAmp    == 0.05f);
        CHECK(sys.config().facingFireRad   == 0.28f);

        sys.setDifficulty(BotDifficulty::Easy);
        CHECK(sys.config().fireRange       == 180.0f);

        // Manual override via setConfig.
        BotConfig custom{};
        custom.fireRange = 999.0f;
        sys.setConfig(custom);
        CHECK(sys.config().fireRange == 999.0f);
    }

    EXIT_WITH_RESULT();
}
