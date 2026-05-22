#include "PlayerInputSystem.hpp"

#include "Input.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rpg {

void PlayerInputSystem::update(threadmaxx::SystemContext& ctx) {
    const auto player = worldState_->player;
    if (!player.valid()) return;

    const auto& w = ctx.world();
    if (!w.alive(player)) return;

    // 2026-05-22 audit fix — bail out cleanly when the player is
    // dead. Pre-fix the input system kept reading WASD, kept
    // setting velocity, and (worse) kept driving the sword-swing
    // animation after the player's HP hit 0. We now drop input
    // entirely once the corpse-disable tag flips on; this also
    // covers the brief window between EntityDied being emitted
    // and RespawnSystem committing the DisabledTag (the Health
    // check matches the same condition the brain uses).
    if (w.hasTag(player, threadmaxx::Component::DisabledTag)) return;
    if (const auto* hp = w.tryGetHealth(player); hp && hp->current <= 0.0f) {
        // Consume edge bits so they don't queue up for a respawn —
        // the press is over by then. Movement axes are continuous,
        // not edges, so they reset naturally on key release.
        (void)takeEdges();
        return;
    }

    const PlayerState* ps =
        threadmaxx::user::tryGet<PlayerState>(w, ids_->playerState, player);
    if (!ps) return;

    // ---- Drain edge events once (atomic exchange).
    const std::uint32_t edges = takeEdges();
    const bool attackEdge    = (edges & kEdgeAttack) != 0;
    const bool blockEdge     = (edges & kEdgeBlock) != 0;
    const bool interactEdge  = (edges & kEdgeInteract) != 0;
    const bool jumpEdge      = (edges & kEdgeJump) != 0;
    const bool cameraToggle  = (edges & kEdgeCameraToggle) != 0;
    const bool sprintEdge    = (edges & kEdgeSprint) != 0;
    if (edges & kEdgeAimToggle) {
        // 2026-05-20 — V toggles the over-the-shoulder PIP. We flip
        // it here because PlayerInputSystem is the one that owns
        // the edges; routing through WorldState gives CameraSystem
        // a stable per-tick read.
        worldState_->aimPipVisible = !worldState_->aimPipVisible;
    }

    PlayerState updated = *ps;
    const float dt = static_cast<float>(ctx.dt());

    // ---- Apply mouse/Q/E yaw delta + pitch delta to PlayerState.
    //
    // The spec routes raw input → mapped (yawDelta/pitchDelta) →
    // intent (yawRadians/pitchRadians on PlayerState) → state
    // (player Transform.orientation built from yaw).
    // PlayerInputSystem owns both the input drain and the player
    // intent fields. CameraSystem reads them back for view-matrix
    // construction.
    updated.yawRadians   += input().yawDelta;
    updated.pitchRadians  = std::clamp(updated.pitchRadians + input().pitchDelta,
                                       kPitchMinRadians, kPitchMaxRadians);
    // 2026-05-22 audit refactor — first/third-person toggle. Pre
    // batch-15a the demo only had a single third-person follow
    // camera; the spec's R-key cycles between first and third.
    if (cameraToggle) updated.firstPerson = updated.firstPerson ? 0u : 1u;

    // ---- Sprint state machine + stamina drain/regen.
    //
    // 2026-05-22 audit (round 2) — Shift edge → sprinting while W/Up
    // is held and stamina is available. Drain while sprinting,
    // regen while not. Sprint cancels on (release-of-W/Up OR stamina
    // exhausted); re-activation requires stamina to recover past
    // `kStaminaResumeThreshold` so the player can't stutter-sprint
    // forever against the floor.
    //
    // 2026-05-22 audit (round 4) — full depletion (sprint drain OR
    // jump cost zeroing) sets `staminaRecoveryDelay = kStaminaRecoveryDelaySeconds`.
    // While the delay is > 0 stamina does NOT regen, giving the
    // exhaustion beat the player feels. Sprint re-activation is
    // also gated on the delay so a player can't stutter the moment
    // the threshold reopens.
    const bool forwardKeyHeld = (input().forwardKeyHeld != 0u);
    if (updated.staminaRecoveryDelay > 0.0f) {
        updated.staminaRecoveryDelay =
            std::max(0.0f, updated.staminaRecoveryDelay - dt);
    }
    if (updated.sprinting != 0u) {
        if (!forwardKeyHeld || updated.stamina <= 0.0f) {
            updated.sprinting = 0u;
            updated.stamina   = std::max(0.0f, updated.stamina);
        }
    } else if (sprintEdge && forwardKeyHeld &&
               updated.staminaRecoveryDelay <= 0.0f &&
               updated.stamina >= kStaminaResumeThreshold) {
        updated.sprinting = 1u;
    }
    if (updated.sprinting != 0u) {
        const float before = updated.stamina;
        updated.stamina = std::max(0.0f, updated.stamina - kStaminaDrainRate * dt);
        if (updated.stamina <= 0.0f) {
            updated.sprinting = 0u;
            if (before > 0.0f) {
                updated.staminaRecoveryDelay = kStaminaRecoveryDelaySeconds;
            }
        }
    } else if (updated.staminaRecoveryDelay <= 0.0f) {
        updated.stamina = std::min(kStaminaMax,
                                   updated.stamina + kStaminaRegenRate * dt);
    }

    // ---- Player-local movement vector.
    //
    // Movement axes from Input come in as raw inputs in player-
    // local space (forward = -Z, strafe = +X). We rotate by yaw to
    // produce world-space velocity. yaw=0 → facing -Z (matches
    // the camera placement in CameraSystem).
    float speed = updated.runSpeed;
    if (updated.sprinting != 0u) speed *= kSprintMultiplier;
    const float yaw   = updated.yawRadians;
    const float cosY  = std::cos(yaw);
    const float sinY  = std::sin(yaw);
    const float forward = input().forward;
    const float strafe  = input().strafe;
    const float vx = (-sinY * forward + cosY * strafe) * speed;
    const float vz = (-cosY * forward - sinY * strafe) * speed;

    // ---- Action timers (decay).
    if (updated.swordSwingTimer > 0.0f) {
        updated.swordSwingTimer = std::max(0.0f, updated.swordSwingTimer - dt);
    }
    if (updated.blockTimer > 0.0f) {
        updated.blockTimer = std::max(0.0f, updated.blockTimer - dt);
    }

    // ---- HP regen + combat timer.
    //
    // 2026-05-22 audit (round 3) — slow passive heal at
    // `kPlayerHpRegenRate` HP/sec, applied as long as the player is
    // alive and not at full HP. Computed here (already inside the
    // single() write path) so it shares the same per-tick commit as
    // the other player-state updates. We use the LIVE Health snapshot
    // (already fetched at the top of update) so the increment composes
    // with damage from the same tick.
    //
    // 2026-05-22 audit (round 4) — combat slowdown. Comparing the
    // live HP snapshot against `prevHp_` (cached system member,
    // initialized to -1.0f sentinel on first tick) detects an
    // incoming hit. On detect, `combatTimer` resets to
    // `kCombatTimerSeconds`; while > 0 the effective regen rate is
    // scaled by `kPlayerHpRegenInCombatScale`. The timer decrements
    // at the same dt below.
    if (updated.combatTimer > 0.0f) {
        updated.combatTimer = std::max(0.0f, updated.combatTimer - dt);
    }
    float newHpCurrent = -1.0f;  // sentinel: don't write
    float newHpMax     = 0.0f;
    if (const auto* hp = w.tryGetHealth(player); hp) {
        // Drop-detection: skip the very first tick (sentinel) and
        // require a non-trivial drop so a regen-write rounding error
        // doesn't accidentally retrigger combat.
        if (prevHp_ >= 0.0f && hp->current + 0.01f < prevHp_) {
            updated.combatTimer = kCombatTimerSeconds;
        }
        prevHp_ = hp->current;
        if (hp->current > 0.0f && hp->current < hp->max) {
            const float regenRate = (updated.combatTimer > 0.0f)
                ? kPlayerHpRegenRate * kPlayerHpRegenInCombatScale
                : kPlayerHpRegenRate;
            const float candidate = std::min(hp->max, hp->current +
                                             regenRate * dt);
            if (candidate != hp->current) {
                newHpCurrent = candidate;
                newHpMax     = hp->max;
            }
        }
    }

    // ---- Action edges.
    if (attackEdge && updated.swordSwingTimer <= 0.0f) {
        updated.swordSwingTimer = kSwordSwingSeconds;
    }
    if (blockEdge) {
        updated.blockTimer = kBlockSeconds;
    }
    if (interactEdge) {
        // Minimal placeholder for interact — the spec's "F must
        // activate nearby interactables when in range" requires a
        // gameplay interactable system that doesn't exist yet.
        // For now the press is observable via the log so a future
        // InteractSystem can subscribe.
        std::printf("[rpg_demo] interact (no nearby target)\n");
    }

    // ---- Jump + vertical velocity integration.
    //
    // The player walks on a heightmap whose Y is rewritten each
    // tick by TerrainAttachSystem. Pre-refactor TerrainAttachSystem
    // ALWAYS snapped Y down to terrain (no jump possible). The
    // refactor's contract: TerrainAttachSystem snaps to ground only
    // when `verticalVel <= 0 && !airborne`, and resets `airborne`
    // when contact resumes. PlayerInputSystem owns the lift
    // integration and the input-edge → verticalVel kick. We
    // integrate Y here so the player's `Transform.position.y`
    // moves smoothly upward during a jump and the camera tracks
    // it via the camera system reading the same field.
    const threadmaxx::Transform& currentT = w.get<threadmaxx::Transform>(player);
    threadmaxx::Transform newT = currentT;
    // 2026-05-22 audit (round 4) — jump now costs `kJumpStaminaCost`.
    // Refused (press silently consumed) if the player is gassed
    // (stamina below cost OR recovery-delay still ticking). Cost is
    // subtracted only on a successful jump. If the deduction zeroes
    // stamina, the depletion delay activates so the next jump /
    // sprint waits for the recovery beat.
    if (jumpEdge && updated.airborne == 0u &&
        updated.staminaRecoveryDelay <= 0.0f &&
        updated.stamina >= kJumpStaminaCost) {
        updated.verticalVel = kJumpVelocity;
        updated.airborne    = 1u;
        const float beforeStamina = updated.stamina;
        updated.stamina    = std::max(0.0f, updated.stamina - kJumpStaminaCost);
        if (updated.stamina <= 0.0f && beforeStamina > 0.0f) {
            updated.staminaRecoveryDelay = kStaminaRecoveryDelaySeconds;
        }
    }
    if (updated.airborne != 0u) {
        updated.verticalVel += kGravity * dt;
        newT.position.y      = currentT.position.y + updated.verticalVel * dt;
    }

    // ---- Sync the player's world-space orientation to the yaw.
    //
    // The sword is Parent-attached and HierarchySystem propagates
    // `position = parent.position + rotate(parent.orientation,
    // local.position)`. CombatSystem's tip computation rotates
    // through the same orientation, so this write keeps both the
    // visible sword and the hit volume aligned with the camera
    // forward.
    const float half = yaw * 0.5f;
    newT.orientation.x = 0.0f;
    newT.orientation.y = std::sin(half);
    newT.orientation.z = 0.0f;
    newT.orientation.w = std::cos(half);

    // ---- Sword swing animation (visible local-offset rotation
    //      tied to swordSwingTimer).
    const auto sword = worldState_->sword;
    const float currSwing = updated.swordSwingTimer;
    const bool  swingActive    = currSwing > 0.0f;
    const bool  swingJustEnded =
        prevSwingTimer_ > 0.0f && currSwing <= 0.0f;
    prevSwingTimer_ = currSwing;

    threadmaxx::Parent swordParent = {};
    bool writeSwordParent = false;
    if (sword.valid() && w.alive(sword) &&
        !w.hasTag(sword, threadmaxx::Component::DisabledTag)) {
        swordParent.parent = player;
        // 2026-05-22 audit refactor — pull the sword closer to the
        // camera when in first-person view so it visibly hangs in
        // the player's right hand. Third-person keeps the legacy
        // hip-front pose.
        //
        // 2026-05-22 audit (round 5) — switch FPV rest pose from a
        // strictly-horizontal blade pointing forward (essentially
        // invisible to the camera — only its near end pokes into
        // view) to a DIAGONAL pose: hilt held mid-chest right of the
        // camera, blade tilted up by ~30° around the local X axis so
        // the blade enters the upper-right of the FPV viewport. The
        // diagonal hides the previous "horizontal sliver" problem
        // without needing to introduce a separate FPV sword model.
        // TPV is unchanged.
        constexpr float kFpvRestPitchRad = 0.52f;  // ~30°
        if (updated.firstPerson != 0u) {
            swordParent.localOffset.position = {0.45f, 0.35f, -0.55f};
            swordParent.localOffset.scale    = {0.12f, 0.12f, 1.0f};
        } else {
            swordParent.localOffset.position = {0.5f, 0.8f, -0.8f};
            swordParent.localOffset.scale    = {0.18f, 0.18f, 1.4f};
        }
        if (swingActive) {
            const float progress =
                1.0f - currSwing / kSwordSwingSeconds;          // 0 → 1
            const float a = kSwingAngleStart +
                            progress * (kSwingAngleEnd - kSwingAngleStart);
            const float halfA = a * 0.5f;
            swordParent.localOffset.orientation = {
                std::sin(halfA), 0.0f, 0.0f, std::cos(halfA),
            };
            writeSwordParent = true;
        } else if (swingJustEnded) {
            // Snap back to the per-mode rest orientation.
            if (updated.firstPerson != 0u) {
                const float halfA = kFpvRestPitchRad * 0.5f;
                swordParent.localOffset.orientation = {
                    std::sin(halfA), 0.0f, 0.0f, std::cos(halfA),
                };
            } else {
                swordParent.localOffset.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
            }
            writeSwordParent = true;
        } else {
            // Re-issue every tick so a first/third-person toggle
            // takes effect immediately (the pre-fix only wrote
            // when the swing state changed).
            if (updated.firstPerson != 0u) {
                const float halfA = kFpvRestPitchRad * 0.5f;
                swordParent.localOffset.orientation = {
                    std::sin(halfA), 0.0f, 0.0f, std::cos(halfA),
                };
            } else {
                swordParent.localOffset.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
            }
            writeSwordParent = true;
        }
    }

    const auto idsPS = ids_->playerState;
    ctx.single([player, vx, vz, updated, idsPS, newT,
                sword, swordParent, writeSwordParent,
                newHpCurrent, newHpMax]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        cb.setVelocity(player, threadmaxx::Velocity{{vx, 0.0f, vz}, {0, 0, 0}});
        cb.setTransform(player, newT);
        threadmaxx::addUserComponent(cb, idsPS, player, updated);
        if (writeSwordParent) cb.setParent(sword, swordParent);
        if (newHpCurrent >= 0.0f) {
            cb.setHealth(player, threadmaxx::Health{newHpCurrent, newHpMax});
        }
    });
}

} // namespace rpg
