// Exercises the new Acceleration component end-to-end:
//   • spawn() carries Acceleration through CommandBuffer / commit / storage
//   • CmdSetAcceleration round-trips
//   • World::tryGetAcceleration / accelerations() expose it
//   • A two-stage physics pipeline (Accel→Vel, then Vel→Transform) integrates
//     correctly: after N steps with v0=0, a=1, dt=1 we expect v=N, p=N(N+1)/2
//     (semi-implicit Euler, since AccelSystem commits before MoveSystem reads).
//
// The two systems conflict on Velocity (one writes, the other reads), so the
// scheduler must place them in distinct waves — that ordering is what makes
// the integration deterministic.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

namespace {

using threadmaxx::Component;
using threadmaxx::ComponentSet;

class AccelSystem : public threadmaxx::ISystem {
    const char* name() const noexcept override { return "accel"; }
    ComponentSet reads()  const noexcept override { return Component::Acceleration; }
    ComponentSet writes() const noexcept override { return Component::Velocity; }
    void update(threadmaxx::SystemContext& ctx) override {
        const auto entities      = ctx.world().entities();
        const auto velocities    = ctx.world().velocities();
        const auto accelerations = ctx.world().accelerations();
        const auto dt = static_cast<float>(ctx.dt());
        ctx.parallelFor(static_cast<std::uint32_t>(entities.size()), /*grain*/ 0,
            [=](threadmaxx::Range r, threadmaxx::CommandBuffer& cb) {
                for (auto i = r.begin; i < r.end; ++i) {
                    threadmaxx::Velocity v = velocities[i];
                    v.linear  = v.linear  + accelerations[i].linear  * dt;
                    v.angular = v.angular + accelerations[i].angular * dt;
                    cb.setVelocity(entities[i], v);
                }
            });
    }
};

class MoveSystem : public threadmaxx::ISystem {
    const char* name() const noexcept override { return "move"; }
    ComponentSet reads()  const noexcept override { return Component::Velocity; }
    ComponentSet writes() const noexcept override { return Component::Transform; }
    void update(threadmaxx::SystemContext& ctx) override {
        const auto entities   = ctx.world().entities();
        const auto transforms = ctx.world().transforms();
        const auto velocities = ctx.world().velocities();
        const auto dt = static_cast<float>(ctx.dt());
        ctx.parallelFor(static_cast<std::uint32_t>(entities.size()), /*grain*/ 0,
            [=](threadmaxx::Range r, threadmaxx::CommandBuffer& cb) {
                for (auto i = r.begin; i < r.end; ++i) {
                    threadmaxx::Transform t = transforms[i];
                    t.position = t.position + velocities[i].linear * dt;
                    cb.setTransform(entities[i], t);
                }
            });
    }
};

class PhysicsGame : public threadmaxx::IGame {
    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        engine.registerSystem(std::make_unique<AccelSystem>());
        engine.registerSystem(std::make_unique<MoveSystem>());
        seed.spawn(threadmaxx::Transform{},
                   threadmaxx::Velocity{},
                   threadmaxx::RenderTag{},
                   threadmaxx::UserData{},
                   threadmaxx::Acceleration{{1.0f, 0.0f, 0.0f}, {}});
    }
};

bool nearly(float a, float b, float eps = 1e-4f) {
    const float d = a - b;
    return (d < 0 ? -d : d) < eps;
}

} // namespace

int main() {
    // Spawn carries Acceleration; pipeline integrates it correctly.
    {
        threadmaxx::Config cfg;
        cfg.sleepToPace = false;
        cfg.fixedStepSeconds = 1.0;  // makes the closed-form integration exact
        threadmaxx::Engine engine(cfg);
        PhysicsGame game;
        CHECK(engine.initialize(game));

        // Pre-tick: seed acceleration is visible, velocity/position still zero.
        const auto entities = engine.world().entities();
        CHECK_EQ(entities.size(), std::size_t{1});
        const auto e = entities[0];
        const auto* a0 = engine.world().tryGetAcceleration(e);
        CHECK(a0 != nullptr);
        CHECK(nearly(a0->linear.x, 1.0f));
        CHECK(nearly(engine.world().tryGetVelocity(e)->linear.x, 0.0f));
        CHECK(nearly(engine.world().tryGetTransform(e)->position.x, 0.0f));

        constexpr int N = 5;
        for (int i = 0; i < N; ++i) engine.step();

        // v_N = N, p_N = N*(N+1)/2 = 15 for N=5.
        CHECK(nearly(engine.world().tryGetVelocity(e)->linear.x,
                     static_cast<float>(N)));
        CHECK(nearly(engine.world().tryGetTransform(e)->position.x,
                     static_cast<float>(N * (N + 1)) / 2.0f));

        // accelerations() dense span matches tryGetAcceleration.
        const auto accels = engine.world().accelerations();
        CHECK_EQ(accels.size(), std::size_t{1});
        CHECK(nearly(accels[0].linear.x, 1.0f));

        engine.shutdown();
    }

    // CmdSetAcceleration round-trips through the commit phase.
    {
        threadmaxx::Config cfg;
        cfg.sleepToPace = false;
        cfg.fixedStepSeconds = 1.0;

        struct SetterSystem : public threadmaxx::ISystem {
            threadmaxx::EntityHandle target;
            bool fired = false;
            const char* name() const noexcept override { return "setter"; }
            // Single system, defaults are fine — runs in its own wave.
            void update(threadmaxx::SystemContext& ctx) override {
                if (fired) return;
                fired = true;
                ctx.single([t = target](threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
                    cb.setAcceleration(t, threadmaxx::Acceleration{{9.0f, 8.0f, 7.0f}, {}});
                });
            }
        };

        threadmaxx::Engine engine(cfg);

        struct SetterGame : public threadmaxx::IGame {
            threadmaxx::EntityHandle* outHandle;
            void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                         threadmaxx::CommandBuffer& seed) override {
                auto sys = std::make_unique<SetterSystem>();
                sysPtr = sys.get();
                engine.registerSystem(std::move(sys));
                seed.spawn(threadmaxx::Transform{}, threadmaxx::Velocity{},
                           threadmaxx::RenderTag{}, threadmaxx::UserData{},
                           threadmaxx::Acceleration{});
                (void)outHandle;
            }
            SetterSystem* sysPtr = nullptr;
        };
        SetterGame game;
        CHECK(engine.initialize(game));
        // After init, the seed buffer has been committed — there is exactly
        // one entity. Capture its handle and pass it into the setter system.
        const auto entities = engine.world().entities();
        CHECK_EQ(entities.size(), std::size_t{1});
        game.sysPtr->target = entities[0];

        engine.step();

        const auto* a = engine.world().tryGetAcceleration(entities[0]);
        CHECK(a != nullptr);
        CHECK(nearly(a->linear.x, 9.0f));
        CHECK(nearly(a->linear.y, 8.0f));
        CHECK(nearly(a->linear.z, 7.0f));

        engine.shutdown();
    }

    EXIT_WITH_RESULT();
}
