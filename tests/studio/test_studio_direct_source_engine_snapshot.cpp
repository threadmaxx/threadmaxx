/// @file test_studio_direct_source_engine_snapshot.cpp
/// @brief ST4 — DirectDataSource on a live engine returns the
/// engine's FrameSnapshot summary with matching tick / timing /
/// pause / system / worker counts.

#include "Check.hpp"
#include "studio/StudioTestFixture.hpp"

#include <threadmaxx_editor/commands.hpp>
#include <threadmaxx_studio/direct_data_source.hpp>

int main() {
    threadmaxx::studio::test::ScopedSession env{};
    threadmaxx::editor::CommandStack stack{env.engine()};
    threadmaxx::studio::DirectDataSource src{env.engine(), stack};

    CHECK(src.mode() == threadmaxx::studio::AttachMode::Direct);
    CHECK(&src.engine() == &env.engine());

    // Drive the engine a handful of ticks so the snapshot has real
    // numbers.
    for (int i = 0; i < 5; ++i) {
        env.engine().step();
    }

    auto snap = src.engineSnapshot();
    CHECK(snap.has_value());
    CHECK_EQ(snap->tick, env.engine().tick());
    CHECK(!snap->paused);
    CHECK_EQ(snap->workerCount, env.engine().workerCount());
    CHECK_EQ(snap->systemCount,
             static_cast<std::uint32_t>(env.engine().registeredSystemCount()));
    // The engine's frameSnapshot.engine.lastStepSeconds is the same
    // value we lifted; if the engine ran any step we should see a
    // non-negative number.
    CHECK(snap->lastStepSeconds >= 0.0);

    // Paused engine flips the bit.
    env.engine().setPaused(true);
    env.engine().step();
    snap = src.engineSnapshot();
    CHECK(snap.has_value());
    CHECK(snap->paused);

    EXIT_WITH_RESULT();
}
