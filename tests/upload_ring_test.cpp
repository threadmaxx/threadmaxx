// §3.2 batch 8: UploadRing — slab rotation, bump allocation, alignment,
// grow-on-overflow behavior.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>

int main() {
    using namespace threadmaxx;

    UploadRing ring(2, 256);
    CHECK_EQ(ring.frameCount(), 2u);
    CHECK_EQ(ring.bytesPerFrame(), std::size_t{256});

    // First frame: allocate something aligned to 16 by default.
    ring.nextFrame();
    void* a = ring.allocate(40);
    CHECK(a != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(a) % 16 == 0);
    CHECK_EQ(ring.head(), std::size_t{40});

    void* b = ring.allocate(10, 8);
    CHECK(b != nullptr);
    // 40 is already 8-byte-aligned, so head advances to 40 + 10 = 50.
    CHECK_EQ(ring.head(), std::size_t{50});

    // Out-of-space without growth -> nullptr.
    void* c = ring.allocate(1000);
    CHECK(c == nullptr);

    // pushBytes returns a sensible offset and writes the data.
    std::uint32_t value = 0xABCDEF01u;
    const auto offset = ring.pushBytes(&value, sizeof(value));
    CHECK(offset != static_cast<std::size_t>(-1));

    // Advance: head resets to 0 on the next slab.
    ring.nextFrame();
    CHECK_EQ(ring.head(), std::size_t{0});
    CHECK_EQ(ring.currentFrame(), 0u);   // wrapped to slab 0 after starting at 1

    // Enabling growth allows oversize allocations.
    ring.setGrowOnOverflow(true);
    void* big = ring.allocate(10000);
    CHECK(big != nullptr);
    CHECK(ring.bytesPerFrame() >= 10000);

    EXIT_WITH_RESULT();
}
