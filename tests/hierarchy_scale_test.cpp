// §3.1 HierarchyConfig::propagateScale: default is off (preserves
// historical behavior — child world scale = local.scale). When opted in,
// scale chains as parent.scale * local.scale component-wise.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

namespace {

class SeedGame : public threadmaxx::IGame {
public:
    threadmaxx::EntityHandle parent;
    threadmaxx::EntityHandle child;
    void onSetup(threadmaxx::Engine& eng, threadmaxx::World&,
                 threadmaxx::CommandBuffer& cb) override {
        parent = eng.reserveEntityHandle();
        child  = eng.reserveEntityHandle();

        threadmaxx::Transform pt;
        pt.position = {0, 0, 0};
        pt.scale = {2.0f, 3.0f, 4.0f};
        cb.spawn(parent, pt);

        threadmaxx::Transform ct;
        ct.scale = {1, 1, 1};
        threadmaxx::Parent p;
        p.parent = parent;
        p.localOffset.position = {1, 0, 0};
        p.localOffset.scale = {0.5f, 0.5f, 0.5f};
        cb.spawn(child, ct, {}, {}, {}, {}, p);
    }
};

bool approx(float a, float b, float tol = 1e-5f) {
    const float d = a - b;
    return (d < tol) && (-d < tol);
}

} // namespace

int main() {
    using namespace threadmaxx;

    // Test 1: default — scale does NOT chain. Child world scale == local.
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        SeedGame g;
        CHECK(engine.initialize(g));
        engine.registerSystem(makeHierarchySystem());  // default cfg
        engine.step();

        const auto* ct = engine.world().tryGetTransform(g.child);
        CHECK(ct != nullptr);
        CHECK(approx(ct->scale.x, 0.5f));
        CHECK(approx(ct->scale.y, 0.5f));
        CHECK(approx(ct->scale.z, 0.5f));
        engine.shutdown();
    }

    // Test 2: propagateScale=true — child world scale == parent * local
    //   = (2, 3, 4) * (0.5, 0.5, 0.5)
    //   = (1.0, 1.5, 2.0)
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
        Engine engine(cfg);
        SeedGame g;
        CHECK(engine.initialize(g));
        HierarchyConfig hcfg;
        hcfg.propagateScale = true;
        engine.registerSystem(makeHierarchySystem(hcfg));
        engine.step();

        const auto* ct = engine.world().tryGetTransform(g.child);
        CHECK(ct != nullptr);
        CHECK(approx(ct->scale.x, 1.0f));
        CHECK(approx(ct->scale.y, 1.5f));
        CHECK(approx(ct->scale.z, 2.0f));
        engine.shutdown();
    }

    EXIT_WITH_RESULT();
}
