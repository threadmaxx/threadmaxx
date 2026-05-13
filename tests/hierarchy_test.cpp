// HierarchySystem: parent→child translation, multi-level chains resolved in
// a single tick, orientation chains correctly, Parent presence bit toggles
// via setParent.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cmath>
#include <memory>

namespace {

using threadmaxx::Component;
using threadmaxx::ComponentSet;
using threadmaxx::EntityHandle;
using threadmaxx::Parent;
using threadmaxx::Quat;
using threadmaxx::Transform;
using threadmaxx::Vec3;

ComponentSet maskWithParent() {
    return ComponentSet{Component::Transform}
         | ComponentSet{Component::Velocity}
         | ComponentSet{Component::UserData}
         | ComponentSet{Component::Acceleration}
         | ComponentSet{Component::Parent};
}
ComponentSet maskRoot() {
    return ComponentSet{Component::Transform}
         | ComponentSet{Component::Velocity}
         | ComponentSet{Component::UserData}
         | ComponentSet{Component::Acceleration};
}

bool nearly(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps;
}

struct HierGame : threadmaxx::IGame {
    EntityHandle root;
    EntityHandle mid;
    EntityHandle leaf;
    // Standalone for the setParent test.
    EntityHandle floater;

    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        engine.registerSystem(threadmaxx::makeHierarchySystem());

        // root at (10, 0, 0), no parent
        seed.commands().emplace_back(threadmaxx::detail::CmdSpawn{
            Transform{Vec3{10, 0, 0}, {}, {1, 1, 1}}, {}, {}, {}, {},
            Parent{},
            maskRoot(),
            &root});

        // mid: child of root, local offset (1, 0, 0) → world (11, 0, 0)
        seed.commands().emplace_back(threadmaxx::detail::CmdSpawn{
            Transform{}, {}, {}, {}, {},
            Parent{/*parent*/ {0, 0}, Transform{Vec3{1, 0, 0}, {}, {1, 1, 1}}},
            maskWithParent(),
            &mid});

        // leaf: child of mid, local offset (0, 2, 0) → world (11, 2, 0)
        seed.commands().emplace_back(threadmaxx::detail::CmdSpawn{
            Transform{}, {}, {}, {}, {},
            Parent{/*parent*/ {0, 0}, Transform{Vec3{0, 2, 0}, {}, {1, 1, 1}}},
            maskWithParent(),
            &leaf});

        // floater: no parent yet
        seed.commands().emplace_back(threadmaxx::detail::CmdSpawn{
            Transform{Vec3{100, 100, 100}, {}, {1, 1, 1}}, {}, {}, {}, {},
            Parent{},
            maskRoot(),
            &floater});
    }
};

// One-shot system that emits a payload of commands on its first update.
class OneShot : public threadmaxx::ISystem {
public:
    threadmaxx::CommandBuffer payload;
    bool fired = false;
    const char* name() const noexcept override { return "oneshot"; }
    void update(threadmaxx::SystemContext& ctx) override {
        if (fired) return;
        fired = true;
        ctx.single([this](threadmaxx::Range, threadmaxx::CommandBuffer& out) {
            for (auto& c : payload.commands()) {
                out.commands().push_back(std::move(c));
            }
        });
    }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
};

} // namespace

int main() {
    threadmaxx::Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;
    threadmaxx::Engine engine(cfg);
    HierGame game;
    CHECK(engine.initialize(game));

    // After onSetup → commit, the captured handles must be valid. Patch
    // the parent fields now that we actually know the assigned handles
    // (CmdSpawn captured zero-handles above as a placeholder), via a
    // OneShot system that emits setParent commands.
    {
        auto fix = std::make_unique<OneShot>();
        fix->payload.setParent(game.mid,
            Parent{game.root, Transform{Vec3{1, 0, 0}, {}, {1, 1, 1}}});
        fix->payload.setParent(game.leaf,
            Parent{game.mid, Transform{Vec3{0, 2, 0}, {}, {1, 1, 1}}});
        engine.registerSystem(std::move(fix));
    }
    // Step once: OneShot fires (commit fixes parents); HierarchySystem
    // also runs but sees the old (zero-handle) parents → no-op.
    engine.step();
    // Step again: HierarchySystem now sees correct parents and resolves
    // the full chain in one pass.
    engine.step();

    const auto& world = engine.world();
    const auto* rootW = world.tryGetTransform(game.root);
    const auto* midW  = world.tryGetTransform(game.mid);
    const auto* leafW = world.tryGetTransform(game.leaf);
    CHECK(rootW != nullptr);
    CHECK(midW  != nullptr);
    CHECK(leafW != nullptr);

    // root stays at its authored world transform.
    CHECK(nearly(rootW->position.x, 10.0f));
    CHECK(nearly(rootW->position.y, 0.0f));
    CHECK(nearly(rootW->position.z, 0.0f));
    // mid = root + (1,0,0)
    CHECK(nearly(midW->position.x, 11.0f));
    CHECK(nearly(midW->position.y, 0.0f));
    CHECK(nearly(midW->position.z, 0.0f));
    // leaf = mid + (0,2,0) — verifying multi-level resolved in one tick.
    CHECK(nearly(leafW->position.x, 11.0f));
    CHECK(nearly(leafW->position.y, 2.0f));
    CHECK(nearly(leafW->position.z, 0.0f));

    // Orientation propagation: rotate root 90° around Z. Identity rotation
    // composed with the rest produces a rotated world transform for child.
    // sin(45°)=cos(45°)=0.7071…; Quat{0,0,sin(θ/2),cos(θ/2)} is 90° about Z.
    const float s = 0.70710678f;
    {
        auto rot = std::make_unique<OneShot>();
        rot->payload.setTransform(game.root,
            Transform{Vec3{10, 0, 0}, Quat{0, 0, s, s}, {1, 1, 1}});
        engine.registerSystem(std::move(rot));
    }
    // HierarchySystem was registered first in onSetup, so within a single
    // step it runs before our OneShot. First step commits the rotation;
    // second step lets hierarchy propagate.
    engine.step();
    engine.step();
    midW = world.tryGetTransform(game.mid);
    CHECK(midW != nullptr);
    // local (1,0,0) rotated 90° about Z = (0,1,0); plus root (10,0,0).
    CHECK(nearly(midW->position.x, 10.0f));
    CHECK(nearly(midW->position.y, 1.0f));
    CHECK(nearly(midW->position.z, 0.0f));

    // Floater has no parent → hierarchy must leave it alone.
    const auto* fW = world.tryGetTransform(game.floater);
    CHECK(fW != nullptr);
    CHECK(nearly(fW->position.x, 100.0f));
    CHECK(nearly(fW->position.y, 100.0f));
    CHECK(nearly(fW->position.z, 100.0f));

    // Attaching a parent flips the Parent presence bit.
    const auto* fMask = world.tryGetComponentMask(game.floater);
    CHECK(fMask != nullptr);
    CHECK(!fMask->has(Component::Parent));
    {
        auto attach = std::make_unique<OneShot>();
        attach->payload.setParent(game.floater,
            Parent{game.root, Transform{Vec3{0, 5, 0}, {}, {1, 1, 1}}});
        engine.registerSystem(std::move(attach));
    }
    // Same reason as above: one step commits setParent, another lets
    // hierarchy compute the new world transform.
    engine.step();
    engine.step();
    fMask = world.tryGetComponentMask(game.floater);
    CHECK(fMask != nullptr);
    CHECK(fMask->has(Component::Parent));
    // root still has Z-rotation; local (0,5,0) rotated 90°-about-Z = (-5,0,0)
    fW = world.tryGetTransform(game.floater);
    CHECK(nearly(fW->position.x, 10.0f + -5.0f));
    CHECK(nearly(fW->position.y, 0.0f));
    CHECK(nearly(fW->position.z, 0.0f));

    // Detaching: setParent(invalid handle) clears the bit.
    {
        auto detach = std::make_unique<OneShot>();
        detach->payload.setParent(game.floater, Parent{});
        engine.registerSystem(std::move(detach));
    }
    engine.step();
    fMask = world.tryGetComponentMask(game.floater);
    CHECK(fMask != nullptr);
    CHECK(!fMask->has(Component::Parent));

    engine.shutdown();
    EXIT_WITH_RESULT();
}
