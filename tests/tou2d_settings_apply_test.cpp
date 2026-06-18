// tou2d_settings_apply_test — N4 (2026-06-18) restart-time settings
// fan-out contract.
//
// Pins the system-level setters added in N4:
//   * `BulletShipCollisionSystem::setDamageScale` clamps to [0, 10]
//     and reproduces the pre-N4 raw `damage` byte when scale = 1.
//   * `ShipLifecycleSystem::setRespawnTicks` clamps to [30, 600] so
//     a corrupted settings.dat can't softlock the round.
//   * `InputSystem::setKeyMap` toggles the polled-keymap branch on
//     (the host wires `settings_.controls` here). The actual GLFW
//     poll isn't exercised; we just verify the setter doesn't
//     trip up an InputSystem that's otherwise been constructed.
//
// TouGame::setSettings is a single-line copy; not pinned by code in
// this file. Round-trip from settings.dat → TouGame → systems is
// exercised by the existing tou2d_settings_io_test for the wire
// shape and by the end-to-end smoke binary for the apply path.

#include "Check.hpp"

#include "../examples/tou2d/BulletShipCollisionSystem.hpp"
#include "../examples/tou2d/DemoTypes.hpp"
#include "../examples/tou2d/InputSystem.hpp"
#include "../examples/tou2d/ShipLifecycleSystem.hpp"

#include <cstdint>

int main() {
    using tou2d::BulletShipCollisionSystem;
    using tou2d::InputSystem;
    using tou2d::ShipLifecycleSystem;
    using tou2d::UserComponentIds;

    // ---- (1) BulletShipCollision damage scale --------------------------
    {
        UserComponentIds ids{};
        BulletShipCollisionSystem sys(ids, /*engine=*/nullptr);

        // Default = 1.0 (pre-N4 behaviour).
        CHECK(sys.damageScale() == 1.0f);

        // Plausible Options preset values pass through unchanged.
        sys.setDamageScale(0.5f);  CHECK(sys.damageScale() == 0.5f);
        sys.setDamageScale(1.5f);  CHECK(sys.damageScale() == 1.5f);
        sys.setDamageScale(2.0f);  CHECK(sys.damageScale() == 2.0f);

        // Negative clamps to 0.0 (pacifist mode).
        sys.setDamageScale(-0.5f); CHECK(sys.damageScale() == 0.0f);

        // Sky-high garbage clamps to 10.0.
        sys.setDamageScale(1e6f);  CHECK(sys.damageScale() == 10.0f);
    }

    // ---- (2) ShipLifecycle respawn ticks -------------------------------
    {
        UserComponentIds ids{};
        ShipLifecycleSystem sys(ids);

        // Default = static kRespawnTicks (180 ticks @ 60 Hz = 3 s).
        CHECK_EQ(sys.respawnTicks(), ShipLifecycleSystem::kRespawnTicks);
        CHECK_EQ(sys.respawnTicks(), std::uint16_t{180});

        // Plausible Options preset values pass through unchanged.
        sys.setRespawnTicks( 90); CHECK_EQ(sys.respawnTicks(), std::uint16_t{ 90});
        sys.setRespawnTicks(180); CHECK_EQ(sys.respawnTicks(), std::uint16_t{180});
        sys.setRespawnTicks(300); CHECK_EQ(sys.respawnTicks(), std::uint16_t{300});

        // Too-short clamps to 30 (0.5 s — UI band floor).
        sys.setRespawnTicks(  0); CHECK_EQ(sys.respawnTicks(), std::uint16_t{ 30});
        sys.setRespawnTicks( 10); CHECK_EQ(sys.respawnTicks(), std::uint16_t{ 30});

        // Too-long clamps to 600 (10 s).
        sys.setRespawnTicks(9999); CHECK_EQ(sys.respawnTicks(), std::uint16_t{600});
    }

    // ---- (3) InputSystem keymap install ------------------------------
    {
        // window/ids both nullable; the setter shouldn't depend on
        // either. We just exercise the runtime path that the host
        // wires up.
        UserComponentIds ids{};
        InputSystem sys(/*window=*/nullptr, ids);
        tou2d::KeyMap km = tou2d::makeDefaultKeyMap();
        sys.setKeyMap(km);
        // No public getter for the install bit, but the setter must
        // accept the call without UB; the actual GLFW poll path is
        // covered by the smoke binary's keyboard-driven gameplay.
    }

    EXIT_WITH_RESULT();
}
