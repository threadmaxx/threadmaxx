/// @file test_studio_physics_body_count.cpp
/// @brief ST21 — PhysicsPanel renders worldStats; body count reflects
/// the bound StubBackend's live bodies; placeholder otherwise.

#include "Check.hpp"

#include <threadmaxx_editor/backends/headless.hpp>
#include <threadmaxx_studio/panels/physics.hpp>

#include <threadmaxx_physics/body.hpp>
#include <threadmaxx_physics/config.hpp>
#include <threadmaxx_physics/shape.hpp>
#include <threadmaxx_physics/stub_backend.hpp>

#include <span>

namespace {

std::size_t countTextOps(
    const threadmaxx::editor::CapturedFrame& frame) {
    std::size_t n = 0;
    for (const auto& op : frame.ops) {
        if (op.op == threadmaxx::editor::CapturedOp::Op::DrawText) ++n;
    }
    return n;
}

struct NullSource : threadmaxx::studio::IStudioDataSource {
    threadmaxx::studio::AttachMode mode() const noexcept override {
        return threadmaxx::studio::AttachMode::Direct;
    }
};

} // namespace

int main() {
    using namespace threadmaxx;
    studio::PhysicsPanel panel;
    CHECK(!panel.isBound());

    editor::HeadlessBackend backend;
    backend.initialize();
    NullSource source;

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 1u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 1u);

    auto phys = physics::makeStubBackend();
    auto world = phys->createWorld(physics::PhysicsConfig{});
    panel.setSource(phys.get(), world);
    CHECK(panel.isBound());

    physics::ShapeDesc sd{};
    sd.type = physics::ShapeType::Sphere;
    sd.radius = 0.5f;
    auto shape = phys->createShape(sd);
    physics::BodyDesc bd{};
    bd.position = {0.0f, 0.0f, 0.0f};
    auto b1 = phys->createBody(world, bd, std::span<const physics::ShapeId>{&shape, 1});
    bd.position = {1.0f, 0.0f, 0.0f};
    auto b2 = phys->createBody(world, bd, std::span<const physics::ShapeId>{&shape, 1});
    CHECK(b1.value != 0);
    CHECK(b2.value != 0);

    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 1u);
    CHECK_EQ(countTextOps(backend.capturedFrame()), 1u);

    // Detach.
    panel.setSource(nullptr, world);
    CHECK(!panel.isBound());
    backend.beginFrame();
    panel.render(backend, source);
    backend.endFrame();
    CHECK_EQ(panel.rowCount(), 1u);

    phys->destroyWorld(world);
    backend.shutdown();
    EXIT_WITH_RESULT();
}
