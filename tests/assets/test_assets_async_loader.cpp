#include "Check.hpp"

#include <chrono>
#include <string>
#include <thread>

#include "threadmaxx_assets/async_loader.hpp"

#define STR2(x) #x
#define STR(x) STR2(x)

using namespace threadmaxx::assets;

int main() {
    const std::string cubePath =
        std::string(STR(THREADMAXX_ASSETS_FIXTURES_DIR)) + "/cube.obj";

    AssetRegistry reg;
    AsyncLoader   async(reg, 2);

    CHECK_EQ(async.workerCount(), std::size_t{2});

    // First enqueue: nothing in the registry → handle starts invalid.
    auto h0 = async.enqueueMesh(cubePath);
    CHECK(!h0.valid());

    // Drain. The worker may take a few ms; spin pump for up to 2 seconds.
    AssetHandle<MeshData> ready;
    for (int i = 0; i < 200; ++i) {
        async.pump();
        ready = reg.loadMesh(cubePath);
        if (ready.valid()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(ready.valid());
    CHECK_EQ(async.failedCount(), std::size_t{0});

    // Subsequent enqueues for the same path dedup synchronously.
    auto h1 = async.enqueueMesh(cubePath);
    CHECK(h1.valid());
    CHECK_EQ(h1.id(), ready.id());

    // Idle pump path: no work queued, no allocations expected. We only
    // verify it doesn't crash and stays cheap; the dedicated zero-alloc
    // gate is the v1.0 close-out crowd test.
    async.pump();
    async.pump();

    EXIT_WITH_RESULT();
}
