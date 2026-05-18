#pragma once

#include <threadmaxx/Game.hpp>

#include <cstdint>
#include <functional>
#include <span>

#include "DemoTypes.hpp"

namespace rpg {

class NPCBrainSystem;

/// IGame implementation. Owns the shared `WorldState` + the registered
/// `UserComponentIds`. Spawns terrain + player + N NPCs + M pickups in
/// `onSetup` and registers every system in dependency order.
class DemoGame : public threadmaxx::IGame {
public:
    /// §3.11 batch 9b.2b — callback the demo uses to upload + register
    /// an OBJ-derived mesh into the renderer. Returns the resulting
    /// `meshId` (>=1) or `-1` on failure. main.cpp installs this with
    /// a lambda that forwards to
    /// `VulkanRenderer::registerMeshFromData`; headless tests leave it
    /// null and the demo falls back to the default cube for every
    /// entity (meshId = 0).
    using RegisterMeshFn = std::function<std::int32_t(
        std::span<const float>, std::span<const std::uint16_t>)>;

    DemoGame();
    ~DemoGame() override;

    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&  world,
                 threadmaxx::CommandBuffer& seed) override;

    WorldState&       worldState()       noexcept { return worldState_; }
    const WorldState& worldState() const noexcept { return worldState_; }

    /// §3.11 batch D-audit — exposes the registered user-component
    /// ids. Tests use this to read / write user components without
    /// re-registering them. Pre-batch-D-audit this was internal-only.
    UserComponentIds&       ids()       noexcept { return ids_; }
    const UserComponentIds& ids() const noexcept { return ids_; }

    /// §3.11 batch 9b.2b — set the mesh-registration callback. Must be
    /// called BEFORE `engine.initialize(game)` (the callback fires
    /// during `onSetup`). Passing `nullptr` clears the callback and
    /// reverts to single-mesh rendering.
    void setRegisterMeshFn(RegisterMeshFn fn) noexcept {
        registerMeshFn_ = std::move(fn);
    }

    /// §3.11 batch 9b.3 — callback invoked by `HudSystem` when the
    /// user hits F12. The demo's main.cpp installs a lambda that
    /// forwards to `VulkanRenderer::reloadShaders`; tests can install
    /// any callable (e.g. a synthetic emit). Must be set BEFORE
    /// `engine.initialize(game)`.
    using ReloadShadersFn = std::function<void()>;
    void setReloadShadersFn(ReloadShadersFn fn) noexcept {
        reloadShadersFn_ = std::move(fn);
    }

private:
    WorldState        worldState_;
    UserComponentIds  ids_;
    RegisterMeshFn    registerMeshFn_;
    ReloadShadersFn   reloadShadersFn_;
    // Owned externally by the engine after `registerSystem` but cached
    // as a raw pointer so PickupSystem can read the spatial hash.
    NPCBrainSystem*   brain_ = nullptr;
};

} // namespace rpg
