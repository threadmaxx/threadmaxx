/// @file test_network_snapshot_schema.cpp
/// @brief NW5 — ReplicationRegistry hashes the wire schema; servers
/// and clients that register the same set of components produce the
/// same hash; divergent registrations produce different hashes.

#include "Check.hpp"

#include <threadmaxx_network/replication.hpp>

int main() {
    using namespace threadmaxx::network;

    ReplicationRegistry a;
    a.registerComponent("Transform", 3);
    a.registerComponent("Health", 2);
    a.registerComponent("Velocity", 2);

    ReplicationRegistry b;
    b.registerComponent("Transform", 3);
    b.registerComponent("Health", 2);
    b.registerComponent("Velocity", 2);
    CHECK_EQ(a.schemaHash(), b.schemaHash());
    CHECK_EQ(a.componentCount(), 3u);

    // Different field counts ⇒ different hash.
    ReplicationRegistry c;
    c.registerComponent("Transform", 3);
    c.registerComponent("Health", 1);   // diverged
    c.registerComponent("Velocity", 2);
    CHECK(a.schemaHash() != c.schemaHash());

    // Re-registering the same name returns the same id.
    auto id1 = a.codecId("Transform");
    CHECK_EQ(id1, 1u);
    CHECK_EQ(a.registerComponent("Transform", 3), id1);
    CHECK_EQ(a.fieldCount(id1), 3u);
    CHECK_EQ(a.codecId("ghost"), 0u);

    EXIT_WITH_RESULT();
}
