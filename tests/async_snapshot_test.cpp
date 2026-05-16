// §3.9.5 batch 20 — async snapshot + async file-trace-sink test.
//
// Two contracts to verify:
//
// (A) `FileTraceSink::setAsync(true)` keeps `onFrame` cheap on the
//     producer thread (sim thread). All N enqueued frames eventually
//     make it to disk; `onShutdown` joins the writer thread cleanly.
//
// (B) `Engine::snapshotAsync(callback)` captures the world state
//     synchronously on the sim thread and invokes `callback` on a
//     background worker. The callback's `WorldSnapshot` reflects
//     the state at the moment `snapshotAsync` was called.
//     Multiple in-flight callbacks queue in submission order. The
//     sim thread keeps stepping while callbacks run.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>
#include <threadmaxx/Serialization.hpp>
#include <threadmaxx/Telemetry.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace threadmaxx;

class SimpleGame : public IGame {
public:
    std::uint32_t initialEntities = 64;
    void onSetup(Engine&, World&, CommandBuffer& cb) override {
        for (std::uint32_t i = 0; i < initialEntities; ++i) {
            Transform t;
            t.position.x = static_cast<float>(i);
            cb.spawn(t);
        }
    }
};

class SpawnEveryTick : public ISystem {
public:
    const char* name() const noexcept override { return "spawner"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        const auto t = ctx.tick();
        ctx.single([t](Range, CommandBuffer& cb) {
            Transform tr;
            tr.position.x = static_cast<float>(t);
            cb.spawn(tr);
        });
    }
};

// Sub-test (A): FileTraceSink::setAsync.
void testAsyncFileTraceSink() {
    const fs::path dir = fs::temp_directory_path() / "tmx_async_trace";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path tmpl = dir / "async.%N.json";

    FileTraceSink::Config cfg;
    cfg.pathTemplate = tmpl.string();
    cfg.rotationBytes = 0;  // no rotation; single file
    FileTraceSink sink(cfg);

    CHECK(!sink.isAsync());
    sink.setAsync(true);
    CHECK(sink.isAsync());

    Config ecfg;
    ecfg.sleepToPace = false;
    Engine engine(ecfg);

    SimpleGame game;
    engine.initialize(game);
    engine.setTraceSink(&sink);

    constexpr int kTicks = 64;
    for (int i = 0; i < kTicks; ++i) engine.step();

    // Tear down — async writer must drain remaining frames + close
    // file cleanly. setAsync(false) joins the writer.
    sink.setAsync(false);
    CHECK(!sink.isAsync());

    engine.setTraceSink(nullptr);
    engine.shutdown();

    // Final file should now be closed (writer dtor not yet called,
    // but the sync drain after setAsync(false) wrote all frames).
    // Explicitly shut down to flush closing ']'.
    sink.onShutdown();

    const fs::path out = dir / "async.0.json";
    CHECK(fs::exists(out));
    std::ifstream f(out, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    CHECK(content.size() > 2);
    CHECK(content.front() == '[');
    // ChromeTraceWriter dtor writes "\n]\n", so trim trailing
    // whitespace before checking for the closing bracket.
    while (!content.empty() &&
           (content.back() == '\n' || content.back() == ' ' ||
            content.back() == '\r' || content.back() == '\t')) {
        content.pop_back();
    }
    CHECK(content.back() == ']');

    fs::remove_all(dir);
}

// Sub-test (B): Engine::snapshotAsync.
void testSnapshotAsync() {
    Config ecfg;
    ecfg.sleepToPace = false;
    Engine engine(ecfg);

    SimpleGame game;
    game.initialEntities = 32;
    engine.initialize(game);
    engine.registerSystem(std::make_unique<SpawnEveryTick>());

    std::mutex                 collectedMtx;
    std::vector<std::uint64_t> collectedSizes;
    std::atomic<int>           callbacksFired{0};

    // Submit one snapshotAsync every 4 ticks across 32 ticks → 8 total.
    constexpr int kTicks = 32;
    constexpr int kEvery = 4;
    for (int i = 0; i < kTicks; ++i) {
        engine.step();
        if (i % kEvery == 0) {
            engine.snapshotAsync([&](WorldSnapshot snap) {
                // Force a small amount of work on the writer thread so
                // any in-tick race shows up as a flaky test.
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                std::lock_guard<std::mutex> lk(collectedMtx);
                collectedSizes.push_back(snap.transforms.size());
                ++callbacksFired;
            });
        }
    }

    // Engine::shutdown joins the snapshot worker. After shutdown
    // returns, every queued callback has fired.
    engine.shutdown();

    CHECK_EQ(callbacksFired.load(), kTicks / kEvery);

    // Callbacks fire in submission order, and the world grew
    // monotonically (SpawnEveryTick added one entity per tick).
    // Therefore the snapshots' transform vectors must be
    // monotonically non-decreasing in size.
    std::vector<std::uint64_t> sizes;
    {
        std::lock_guard<std::mutex> lk(collectedMtx);
        sizes = collectedSizes;
    }
    CHECK_EQ(sizes.size(), static_cast<std::size_t>(kTicks / kEvery));
    for (std::size_t i = 1; i < sizes.size(); ++i) {
        CHECK(sizes[i] >= sizes[i - 1]);
    }
    // The last snapshot's size must be at most game.initialEntities +
    // kTicks (one spawn per tick). It should be at least
    // game.initialEntities + (kTicks - kEvery) since the last snapshot
    // was taken after tick `kTicks-1`.
    CHECK(sizes.back() >= 32u + static_cast<std::uint64_t>(kTicks - kEvery));
    CHECK(sizes.back() <= 32u + static_cast<std::uint64_t>(kTicks));
}

} // namespace

int main() {
    testAsyncFileTraceSink();
    testSnapshotAsync();
    std::printf("[OK] async_snapshot_test\n");
    return 0;
}
