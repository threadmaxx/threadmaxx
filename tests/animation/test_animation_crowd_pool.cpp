#include "Check.hpp"
#include "threadmaxx_animation/detail/job_batch.hpp"
#include "threadmaxx_animation/pose.hpp"

#include <cstddef>

using namespace threadmaxx::animation;
using detail::PoseBufferPool;

int main() {
    // === 1. Empty pool: acquire returns a fresh buffer sized to the
    // requested joint count.
    {
        PoseBufferPool pool;
        CHECK(pool.empty());
        PoseBuffer buf = pool.acquire(8);
        CHECK_EQ(buf.size(), std::size_t{8});
        CHECK(pool.empty());  // acquire does not pre-stash.
    }

    // === 2. release then acquire returns the same buffer (LIFO),
    // resized to the new joint count if it differs.
    {
        PoseBufferPool pool;
        PoseBuffer buf1 = pool.acquire(4);
        buf1.localPose().joints[0].translation = {1.0f, 2.0f, 3.0f};
        pool.release(std::move(buf1));
        CHECK_EQ(pool.size(), std::size_t{1});

        // Re-acquire at SAME size — the slot contents are reused. We
        // don't promise the values survive (pool is opaque storage), but
        // size MUST match.
        PoseBuffer buf2 = pool.acquire(4);
        CHECK_EQ(buf2.size(), std::size_t{4});
        CHECK(pool.empty());

        // Resize on acquire: release 4-joint, acquire 16-joint → buffer
        // grows.
        pool.release(std::move(buf2));
        PoseBuffer buf3 = pool.acquire(16);
        CHECK_EQ(buf3.size(), std::size_t{16});
    }

    // === 3. LIFO discipline: release A, B, C → acquire returns C, B, A.
    {
        PoseBufferPool pool;
        PoseBuffer a = pool.acquire(2);
        PoseBuffer b = pool.acquire(4);
        PoseBuffer c = pool.acquire(8);
        pool.release(std::move(a));
        pool.release(std::move(b));
        pool.release(std::move(c));
        CHECK_EQ(pool.size(), std::size_t{3});

        // LIFO: most-recently-released comes back first. Sizes 8, 4, 2.
        // We probe sizes BEFORE the acquire's own resize, by calling
        // acquire with the matching size so resize is a no-op.
        PoseBuffer p1 = pool.acquire(8);  // was c
        CHECK_EQ(p1.size(), std::size_t{8});
        PoseBuffer p2 = pool.acquire(4);  // was b
        CHECK_EQ(p2.size(), std::size_t{4});
        PoseBuffer p3 = pool.acquire(2);  // was a
        CHECK_EQ(p3.size(), std::size_t{2});
        CHECK(pool.empty());
    }

    // === 4. reserve() pre-grows the pool. After reserve(8, 16), eight
    // acquires return without allocating new buffers (size() drops).
    {
        PoseBufferPool pool;
        pool.reserve(8, 16);
        CHECK_EQ(pool.size(), std::size_t{8});
        for (std::size_t i = 0; i < 8; ++i) {
            PoseBuffer buf = pool.acquire(16);
            CHECK_EQ(buf.size(), std::size_t{16});
            // Drop on the floor — buf destructed at scope end.
        }
        CHECK(pool.empty());
    }

    // === 5. Steady-state pattern: warm-up acquire+release pairs, then
    // a per-tick acquire+release cycle stays at constant pool size.
    {
        PoseBufferPool pool;
        // Warm-up: simulate a worker that processes 4 agents per tick
        // and uses the pool to hold scratch.
        for (std::size_t warm = 0; warm < 4; ++warm) {
            PoseBuffer b = pool.acquire(32);
            pool.release(std::move(b));
        }
        CHECK_EQ(pool.size(), std::size_t{1});  // 1 buffer recycled

        // Per-tick pattern: 4 borrow + return pairs. Pool size stays at
        // 1 the whole time.
        for (std::size_t tick = 0; tick < 10; ++tick) {
            PoseBuffer b = pool.acquire(32);
            CHECK_EQ(b.size(), std::size_t{32});
            pool.release(std::move(b));
            CHECK_EQ(pool.size(), std::size_t{1});
        }
    }

    EXIT_WITH_RESULT();
}
