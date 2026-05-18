// §3.10.3 batch 23 — F11 regression test: `Engine::userComponent<T>()`
// looks up a previously-registered UserComponentId without
// re-registering, and returns an invalid id for unknown types.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdio>

namespace {
struct Foo { float v = 0.0f; };
struct Bar { int   v = 0; };
struct NeverRegistered { float v = 0.0f; };
} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg;
    cfg.sleepToPace = false;
    Engine engine(cfg);

    struct G : IGame {
        UserComponentId fooId;
        UserComponentId barId;
        void onSetup(Engine& e, World&, CommandBuffer&) override {
            fooId = e.registerUserComponent<Foo>();
            barId = e.registerUserComponent<Bar>();
        }
    } game;
    CHECK(engine.initialize(game));

    // §3.10.3 F11: lookup returns the same id as registration.
    const auto fooLookup = engine.userComponent<Foo>();
    CHECK(fooLookup.valid());
    CHECK_EQ(fooLookup.bit, game.fooId.bit);
    CHECK_EQ(fooLookup.stride, game.fooId.stride);

    const auto barLookup = engine.userComponent<Bar>();
    CHECK(barLookup.valid());
    CHECK_EQ(barLookup.bit, game.barId.bit);

    // Unknown type → invalid id, NOT auto-registered.
    const auto missing = engine.userComponent<NeverRegistered>();
    CHECK(!missing.valid());
    std::printf("[engine_user_component_lookup] foo bit=%u bar bit=%u "
                "missing valid=%d\n",
                fooLookup.bit, barLookup.bit, int(missing.valid()));

    // Re-registration returns the same id (engine spec).
    const auto fooReg2 = engine.registerUserComponent<Foo>();
    CHECK_EQ(fooReg2.bit, game.fooId.bit);

    EXIT_WITH_RESULT();
}
