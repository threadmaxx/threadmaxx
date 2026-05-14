// §3.2 batch 8: InstanceLayoutEntry has the documented stable shape;
// packInstance round-trips the documented fields from a DrawItem.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

int main() {
    using namespace threadmaxx;

    // Shape contract: 128 bytes, 16-byte aligned.
    CHECK_EQ(sizeof(InstanceLayoutEntry), std::size_t{128});
    CHECK(alignof(InstanceLayoutEntry) >= 16);

    DrawItem item;
    item.entity = EntityHandle{7, 3};
    item.transform.position = {1.0f, 2.0f, 3.0f};
    item.transform.orientation = {0.1f, 0.2f, 0.3f, 0.95f};
    item.transform.scale = {2.0f, 2.0f, 2.0f};
    item.meshId = 42;
    item.materialId = 13;
    item.skeletonId = 5;
    item.pose.ringSlot = 9;
    item.materialOverride.params = {0.5f, 0.25f, 0.125f, 1.0f};
    item.flags = 0xCAFEBABEu;
    item.sortKey = (std::uint64_t{0xDEADBEEFu} << 32) | std::uint64_t{0xFEEDFACEu};

    auto entry = packInstance(item);
    CHECK(entry.worldPosition[0] == 1.0f);
    CHECK(entry.worldPosition[1] == 2.0f);
    CHECK(entry.worldPosition[2] == 3.0f);
    CHECK(entry.worldOrientation[3] == 0.95f);
    CHECK(entry.worldScale[1] == 2.0f);
    CHECK_EQ(entry.meshId, 42);
    CHECK_EQ(entry.materialId, 13);
    CHECK_EQ(entry.skeletonId, 5);
    CHECK_EQ(entry.poseRingSlot, 9);
    CHECK(entry.materialOverride[0] == 0.5f);
    CHECK_EQ(entry.flags, 0xCAFEBABEu);
    CHECK_EQ(entry.sortKeyLow, 0xFEEDFACEu);
    CHECK_EQ(entry.sortKeyHigh, 0xDEADBEEFu);
    CHECK_EQ(entry.entityIndex, 7u);

    // Batch projection writes the same data with the same indexing.
    std::vector<DrawItem> items(3, item);
    std::vector<InstanceLayoutEntry> dst(3);
    const auto n = packInstances(items, dst);
    CHECK_EQ(n, std::size_t{3});
    CHECK_EQ(dst[2].meshId, 42);

    EXIT_WITH_RESULT();
}
