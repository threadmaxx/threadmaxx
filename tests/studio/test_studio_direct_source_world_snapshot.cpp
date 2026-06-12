/// @file test_studio_direct_source_world_snapshot.cpp
/// @brief ST4 — DirectDataSource::worldSnapshot() returns
/// engine.world().snapshot() byte-for-byte. Serializes both via the
/// engine's wire format and compares the produced blobs.

#include "Check.hpp"
#include "studio/StudioTestFixture.hpp"

#include <threadmaxx_editor/commands.hpp>
#include <threadmaxx_studio/direct_data_source.hpp>

#include <threadmaxx/Serialization.hpp>
#include <threadmaxx/World.hpp>

#include <sstream>

int main() {
    threadmaxx::studio::test::ScopedSession env{};
    threadmaxx::editor::CommandStack stack{env.engine()};
    threadmaxx::studio::DirectDataSource src{env.engine(), stack};

    // Take a baseline snapshot from both paths and serialize.
    const auto refSnap = env.engine().world().snapshot();
    const auto viaSrc = src.worldSnapshot();

    std::ostringstream refOss;
    std::ostringstream viaSrcOss;
    threadmaxx::serialize(refOss, refSnap);
    threadmaxx::serialize(viaSrcOss, viaSrc);

    CHECK(refOss.str() == viaSrcOss.str());
    CHECK_EQ(refSnap.size(), viaSrc.size());

    // Step a few ticks; both paths must still agree byte-for-byte.
    for (int i = 0; i < 4; ++i) {
        env.engine().step();
    }
    const auto refSnap2 = env.engine().world().snapshot();
    const auto viaSrc2 = src.worldSnapshot();
    std::ostringstream r2, v2;
    threadmaxx::serialize(r2, refSnap2);
    threadmaxx::serialize(v2, viaSrc2);
    CHECK(r2.str() == v2.str());

    EXIT_WITH_RESULT();
}
