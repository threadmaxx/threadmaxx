/// @file panels/PhysicsPanel.cpp
/// @brief ST21 — `PhysicsPanel` implementation.

#include <threadmaxx_studio/panels/physics.hpp>

#include <threadmaxx_editor/backend.hpp>

#include <threadmaxx_physics/backend.hpp>
#include <threadmaxx_physics/diagnostics.hpp>

#include <cstdio>

namespace threadmaxx::studio {

PhysicsPanel::PhysicsPanel(physics::IPhysicsBackend& backend,
                           physics::PhysicsWorldId   world) noexcept
    : backend_(&backend), world_(world) {}

void PhysicsPanel::setSource(physics::IPhysicsBackend* backend,
                             physics::PhysicsWorldId   world) noexcept {
    backend_ = backend;
    world_   = world;
}

void PhysicsPanel::render(editor::IEditorBackend& backend,
                          IStudioDataSource&) {
    if (backend_ == nullptr) {
        backend.drawText("Physics: <detached>", 0.0f, 0.0f);
        lastRows_ = 1;
        return;
    }
    const auto s = physics::sampleWorldStats(*backend_, world_);
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "Physics  world=%llu  bodies=%zu  constraints=%zu  contacts=%zu",
                  static_cast<unsigned long long>(world_.value),
                  s.bodyCount, s.constraintCount, s.activeContactCount);
    backend.drawText(buf, 0.0f, 0.0f);
    lastRows_ = 1;
}

} // namespace threadmaxx::studio
