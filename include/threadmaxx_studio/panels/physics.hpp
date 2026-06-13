#pragma once

/// @file panels/physics.hpp
/// @brief ST21 — `PhysicsPanel` polls
/// `physics::IPhysicsBackend::worldStats` (the P10 surface) for
/// one bound world and renders a body / constraint / contact
/// summary.

#include "../panel.hpp"

#include <threadmaxx_physics/types.hpp>

#include <cstddef>
#include <string_view>

namespace threadmaxx::physics {
class IPhysicsBackend;
} // namespace threadmaxx::physics

namespace threadmaxx::studio {

class PhysicsPanel : public IStudioPanel {
public:
    PhysicsPanel() noexcept = default;
    PhysicsPanel(physics::IPhysicsBackend& backend,
                 physics::PhysicsWorldId   world) noexcept;

    /// @brief Bind (or detach) a backend + world. Pass nullptr to
    /// detach.
    void setSource(physics::IPhysicsBackend* backend,
                   physics::PhysicsWorldId   world) noexcept;

    std::string_view id() const noexcept override {
        return "sibling.physics";
    }
    std::string_view title() const noexcept override { return "Physics"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    [[nodiscard]] std::size_t rowCount() const noexcept { return lastRows_; }
    [[nodiscard]] bool        isBound()  const noexcept {
        return backend_ != nullptr;
    }

private:
    physics::IPhysicsBackend* backend_{nullptr};
    physics::PhysicsWorldId   world_{};
    std::size_t               lastRows_{0};
};

} // namespace threadmaxx::studio
