// Regression test for the missed-wakeup hang fixed on 2026-05-25.
//
// Pre-fix: `JobSystem::submit()` notified only the round-robin target
// worker's per-worker CV. If that target was busy executing an outer
// job, the notify was wasted — and other parked workers, whose last
// trySteal scan happened before the queue push, stayed parked because
// no signal reached their CVs. Under nested parallelism (an outer job
// that submits inner jobs and waits for them via a latch/CV inside its
// own body) this manifested as an intermittent freeze on machines with
// many cores — rpg_demo on a 72-core box hung roughly 1 in 5 runs.
//
// Post-fix: a global counting semaphore (`workSem_`) carries the wake
// signal. Every successful `submit()` releases one durable permit; any
// parked worker may consume it. A busy target no longer strands work.
//
// This test reproduces the exact nested-submit pattern at high worker
// counts. Pre-fix the high-pool iteration count would hang well before
// completing under the 60-second per-config watchdog; post-fix the
// whole suite (high pool + three small-pool controls) finishes in a
// few seconds.

#include "Check.hpp"
#include "../src/JobSystem.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

using threadmaxx::internal::JobSystem;
using namespace std::chrono_literals;

namespace {

// One iteration of the nested-parallelism pattern: an outer job that
// submits `kInner` inner jobs and waits for all of them to complete
// via a local CV — identical in shape to the rpg_demo freeze (an outer
// wave system body running on a worker, doing a `parallelFor` inside).
//
// With high worker counts, round-robin submission distributes some of
// the inner jobs to the outer's own worker. That worker is blocked
// inside `innerCv.wait`, so it can't pop them itself — they MUST be
// stolen by other workers. Pre-fix, if the parked workers' last
// trySteal scan happened before those inner jobs were pushed, no
// notify reached them and they stayed parked forever.
void runNestedIteration(JobSystem& js, int kInner) {
    std::atomic<int>        innerRemaining{kInner};
    std::mutex              innerMtx;
    std::condition_variable innerCv;

    js.submit([&, kInner] {
        for (int i = 0; i < kInner; ++i) {
            js.submit([&innerRemaining, &innerMtx, &innerCv] {
                if (innerRemaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    std::lock_guard<std::mutex> lk(innerMtx);
                    innerCv.notify_all();
                }
            });
        }
        std::unique_lock<std::mutex> lk(innerMtx);
        innerCv.wait(lk, [&] {
            return innerRemaining.load(std::memory_order_acquire) == 0;
        });
    });

    js.waitIdle();
    CHECK_EQ(innerRemaining.load(std::memory_order_acquire), 0);
}

// Run the nested-iteration pattern with a per-test JobSystem and a
// background watchdog thread that aborts the process on hang. The
// watchdog only fires if the whole loop exceeds 60s — well over the
// few-seconds steady-state runtime.
void runNestedTest(std::uint32_t workers, int kInner, int iters) {
    JobSystem js(workers);

    std::atomic<bool> doneFlag{false};
    std::thread watchdog([&] {
        const auto deadline = std::chrono::steady_clock::now() + 60s;
        while (!doneFlag.load(std::memory_order_acquire)) {
            if (std::chrono::steady_clock::now() > deadline) {
                std::fprintf(stderr,
                    "FATAL: nested-parallelism hang (workers=%u, kInner=%d)\n",
                    workers, kInner);
                std::abort();
            }
            std::this_thread::sleep_for(100ms);
        }
    });

    for (int it = 0; it < iters; ++it) {
        runNestedIteration(js, kInner);
    }

    doneFlag.store(true, std::memory_order_release);
    watchdog.join();
}

} // namespace

int main() {
    // High-pool case — this is the configuration that reliably hung
    // pre-fix. We clamp to at least 8 in case the test host has very
    // few cores; the bug scales with `workerCount` so the hardware
    // value (typically >= 8 on developer boxes / CI) is what we want.
    const std::uint32_t hwWorkers =
        std::max(8u, std::thread::hardware_concurrency());
    runNestedTest(hwWorkers, /*kInner*/ 128, /*iters*/ 200);

    // Small-pool controls — these passed pre-fix too, but we re-run
    // them to catch any regression in low-worker semantics.
    runNestedTest(/*workers*/ 4,  /*kInner*/ 16, /*iters*/ 200);
    runNestedTest(/*workers*/ 8,  /*kInner*/ 32, /*iters*/ 200);
    runNestedTest(/*workers*/ 16, /*kInner*/ 64, /*iters*/ 200);

    EXIT_WITH_RESULT();
}
