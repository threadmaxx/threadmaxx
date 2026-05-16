// §3.11.1 batch D1 — floating-text HP indicators.
//
// For each entity with `Health` whose `current < max` AND not
// DisabledTag-marked, pushes one `DebugText` of the form "HP: 18/60"
// at a position offset above the entity. Uses the owning-string
// `addDebugText` overload (§3.6.5 batch 15a) so the formatted strings
// survive into the next published RenderFrame without the system
// having to maintain its own backing storage.
//
// Runs in `buildRenderFrame` (the render-prep hook), not `update`, so
// the text is emitted once per frame regardless of wave count.

#pragma once

#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

namespace rpg {

class HealthBarSystem : public threadmaxx::ISystem {
public:
    HealthBarSystem() = default;

    const char* name() const noexcept override { return "health-bar"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Health}
             | threadmaxx::ComponentSet{threadmaxx::Component::Transform};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }

    /// No per-tick work — the floating text is emitted by the
    /// `buildRenderFrame` hook instead.
    void update(threadmaxx::SystemContext&) override {}
    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override;

    /// `buildRenderFrame` only sees the `RenderFrameBuilder`, not the
    /// World — stash a pointer at registration time so the hook can
    /// read chunk data when it fires.
    void onRegister(threadmaxx::World& w) override { world_ = &w; }

private:
    const threadmaxx::World* world_ = nullptr;
};

} // namespace rpg
