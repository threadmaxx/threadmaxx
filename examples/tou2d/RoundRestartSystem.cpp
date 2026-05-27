#include "RoundRestartSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace tou2d {

namespace {

/// Restart accepted on ANY of these keys — covers P1 fire (RShift),
/// generic "fire / continue" idiom (Space / Enter), and Numpad-0 (P4
/// fire). A spectator session with no human (P1 is a bot) still
/// accepts Space/Enter from whatever observer is at the keyboard.
bool restartPressed(GLFWwindow* w) noexcept {
    if (!w) return false;
    return glfwGetKey(w, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS ||
           glfwGetKey(w, GLFW_KEY_SPACE)       == GLFW_PRESS ||
           glfwGetKey(w, GLFW_KEY_ENTER)       == GLFW_PRESS ||
           glfwGetKey(w, GLFW_KEY_KP_0)        == GLFW_PRESS;
}

} // namespace

RoundRestartSystem::RoundRestartSystem(GLFWwindow* window,
                                       UserComponentIds ids) noexcept
    : window_(window), ids_(ids) {}

void RoundRestartSystem::preStep(threadmaxx::SystemContext& ctx) {
    if (!roundEnded_) return;
    const bool ended = roundEnded_->load(std::memory_order_acquire);

    // Holdoff bookkeeping — re-arm on the rising edge (live → ended) so
    // the player has at least kRestartHoldoffTicks ticks to recognize
    // the winner banner before the restart key counts.
    if (ended && !wasRoundEnded_) {
        holdoffTicks_ = kRestartHoldoffTicks;
    }
    wasRoundEnded_ = ended;

    if (!ended) return;

    if (holdoffTicks_ > 0) {
        holdoffTicks_ = static_cast<std::uint16_t>(holdoffTicks_ - 1);
        return;
    }

    if (!restartPressed(window_)) return;

    // ---- Round reset ---------------------------------------------------
    const auto idsLp   = ids_.localPlayer;
    const auto idsShip = ids_.ship;
    const auto idsLd   = ids_.loadout;
    if (!idsLp.valid() || !idsShip.valid()) return;

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsLp.componentBit()))    continue;
            if (!chunk.mask.has(idsShip.componentBit()))  continue;
            if (!chunk.mask.has(threadmaxx::Component::Transform)) continue;

            const auto entities = chunk.entities;
            const auto shipSpan = threadmaxx::user::chunkSpan<Ship>(chunk, idsShip);
            const bool wasDisabled =
                chunk.mask.has(threadmaxx::Component::DisabledTag);

            for (std::size_t row = 0, n = entities.size(); row < n; ++row) {
                Ship s = shipSpan[row];
                // Position: random Air cell (M4.4) when a grid is
                // wired in; falls back to the baked-in spawn point
                // if random sampling fails or no grid was installed
                // (host-side tests without a world).
                float rx = s.spawnX;
                float ry = s.spawnY;
                if (grid_) {
                    sampleRandomRespawn(*grid_, rng_, rx, ry);
                }
                threadmaxx::Transform t{};
                t.position = {rx, ry, 0.0f};
                t.scale    = {28.0f, 28.0f, 28.0f};
                cb.setTransform(entities[row], t);

                // Velocity: full stop.
                threadmaxx::Velocity v{};
                cb.setVelocity(entities[row], v);

                // Ship: HP, kills, respawn counters all reset.
                s.currentHp = s.maxHp;
                s.kills     = 0;
                s.respawnIn = 0;
                threadmaxx::addUserComponent(cb, idsShip, entities[row], s);

                // Loadout: full mags, no reload pending.
                if (idsLd.valid()) {
                    WeaponLoadout fresh{};
                    fresh.dumbfireAmmo     = kDumbfireMagazine;
                    fresh.dumbfireReloadIn = 0;
                    fresh.spreadAmmo       = kSpreadMagazine;
                    fresh.spreadReloadIn   = 0;
                    threadmaxx::addUserComponent(cb, idsLd, entities[row], fresh);
                }

                // Tag flips: re-enable any DisabledTag (covers both DM
                // respawning-mid-tick and LSS permanent-dead ships).
                if (wasDisabled) {
                    cb.removeTag(entities[row],
                                 threadmaxx::Component::DisabledTag);
                }
            }
        }
    });

    // Clear the shared round-end state so the next tick's gates open.
    roundEnded_->store(false, std::memory_order_release);
    if (winnerSlot_)  *winnerSlot_  = 0;
    if (winnerKills_) *winnerKills_ = 0;
    holdoffTicks_  = kRestartHoldoffTicks;
    wasRoundEnded_ = false;
}

} // namespace tou2d
