// forEach<Components...> visits every entity exactly once and passes the
// matching component refs.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>

namespace {

std::atomic<int> gVisits{0};

class TouchAllSystem : public threadmaxx::ISystem {
    const char* name() const noexcept override { return "touch"; }
    void update(threadmaxx::SystemContext& ctx) override {
        threadmaxx::forEach<threadmaxx::Transform, threadmaxx::Velocity>(ctx,
            [](threadmaxx::EntityHandle e,
               const threadmaxx::Transform& t,
               const threadmaxx::Velocity& v,
               threadmaxx::CommandBuffer& cb) {
                gVisits.fetch_add(1, std::memory_order_relaxed);
                // Drive a setTransform to verify the (handle, component)
                // pairing the helper hands us is consistent: nudge by v.
                threadmaxx::Transform next = t;
                next.position = t.position + v.linear;
                cb.setTransform(e, next);
            });
    }
};

class TouchGame : public threadmaxx::IGame {
    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        engine.registerSystem(std::make_unique<TouchAllSystem>());
        for (int i = 0; i < 50; ++i) {
            threadmaxx::Velocity v;
            v.linear = {1.0f, 0.0f, 0.0f};
            seed.spawn({}, v);
        }
    }
};

} // namespace

int main() {
    threadmaxx::Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 4;
    threadmaxx::Engine engine(cfg);
    TouchGame game;
    CHECK(engine.initialize(game));

    engine.step();
    CHECK_EQ(gVisits.load(), 50);

    // After one step, every entity's x position should equal v.linear.x * 1
    // (one frame at v={1,0,0}).
    for (const auto& t : engine.world().transforms()) {
        CHECK(t.position.x == 1.0f);
    }

    // Second step doubles it.
    engine.step();
    CHECK_EQ(gVisits.load(), 100);
    for (const auto& t : engine.world().transforms()) {
        CHECK(t.position.x == 2.0f);
    }

    engine.shutdown();
    EXIT_WITH_RESULT();
}
