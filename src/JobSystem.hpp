#pragma once

#include "threadmaxx/Stats.hpp"
#include "threadmaxx/System.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace threadmaxx::internal {

// Fixed-size worker pool with per-worker work-stealing deques. Submit calls
// distribute jobs round-robin across workers (each worker has its own
// queue+mutex+CV, so producers and consumers don't serialize on a single
// hot mutex). Idle workers steal from siblings before sleeping. Designed
// for "submit a batch of jobs, wait for the whole batch" — which is what
// per-system parallel-for needs.
//
// Determinism notes: the *execution order* across workers is not
// deterministic — but the per-job CommandBuffer pattern and the
// commit-in-submission-order rule in EngineImpl make this OK. Workers
// can race freely without affecting world state.
class JobSystem {
public:
    using JobFn = std::function<void()>;

    // workerCount=0 means "pick a sensible default".
    explicit JobSystem(std::uint32_t workerCount);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Submit a job. Increments the outstanding-job counter and pushes onto
    // a worker's local queue (round-robin by submit count). The optional
    // @p priority steers which per-worker deque the job lands in
    // (§3.5 batch 12); workers prefer higher-priority deques both in
    // their own pop path and in cross-worker steals.
    void submit(JobFn fn, JobPriority priority = JobPriority::Normal);

    // Block until every submitted job has finished. Safe to call again.
    void waitIdle();

    std::uint32_t workerCount() const noexcept {
        return static_cast<std::uint32_t>(workers_.size());
    }

    /// Number of jobs queued + in-flight right now. Snapshot reading
    /// is intentionally cheap (single atomic load); a system can
    /// sample this immediately after a `parallelFor` submit to track
    /// peak wave congestion.
    std::uint32_t outstanding() const noexcept {
        return outstanding_.load(std::memory_order_relaxed);
    }

    /// Aggregate worker-pool counters. Cheap to call (atomic loads only)
    /// — safe from any thread.
    JobSystemStats stats() const noexcept;

private:
    struct Worker {
        // §3.5 batch 12: three per-priority deques (High / Normal /
        // Low). Workers' own pops and cross-worker steals scan in
        // priority order so the work-stealing scheduler honors the
        // priority hint. Backwards-compatible: pre-batch-12 calls go
        // through `Normal` and behave bit-for-bit as before.
        std::array<std::deque<JobFn>, kJobPriorityLevels> queues;
        std::mutex              mtx;
        std::condition_variable cv;
        std::thread             thread;
        // Written by the owning worker only; read by `stats()` from any
        // thread. Atomic with relaxed memory ordering so TSAN can see the
        // synchronization (the prior plain-`uint64_t` design was a known
        // benign race — TSAN flagged it correctly). Relaxed because the
        // reader doesn't synchronize with other state through these
        // counters; a slightly stale value is acceptable.
        std::atomic<std::uint64_t> ownPops    {0};
        std::atomic<std::uint64_t> stolenJobs {0};
        // Per-worker job-duration histogram. Same rationale as above —
        // worker-local increment, read-from-anywhere stats path.
        std::array<std::atomic<std::uint64_t>, kJobDurationHistogramBins> histogram{};

        // True iff any of the priority deques has work.
        bool hasWork() const noexcept {
            for (const auto& q : queues) if (!q.empty()) return true;
            return false;
        }
    };

    /// Compute the histogram bin for a job duration. Log2-spaced in
    /// microseconds; saturating at `kJobDurationHistogramBins - 1` for
    /// very long jobs. Public for test coverage.
    static std::size_t binFor(std::chrono::nanoseconds duration) noexcept;

    void workerLoop(std::uint32_t selfIdx);
    // Tries to take a job from one of the other workers' queues. Uses
    // try_lock to avoid blocking on producers and skips queues that are
    // currently being pushed to.
    JobFn trySteal(std::uint32_t selfIdx) noexcept;

    std::vector<std::unique_ptr<Worker>> workers_;

    // Missed-wakeup fix (2026-05-25). The pre-2026-05-25 design used
    // per-worker CVs with predicate `self.hasWork() || stopping_`. That
    // predicate prevented parked workers from escaping `cv.wait` after
    // work was pushed onto a SIBLING'S queue: any notify whose work
    // didn't land on their own queue left the predicate false and they
    // re-parked immediately. With work stranded on a busy worker's
    // queue (e.g. a worker blocked inside `JobLatch::wait` from a
    // nested `parallelFor`), no parked worker could escape `cv.wait`
    // to steal it. Result: intermittent ~1/5 hang on rpg_demo
    // `--stress` at 71 workers.
    //
    // Fix: three cooperating pieces.
    //   1. `wakeSeq_` is bumped (release) by every successful
    //      `submit()` AFTER the queue push. Workers snapshot it
    //      BEFORE incrementing `parkedCount_` and entering `cv.wait`.
    //      The park predicate is extended to `self.hasWork() ||
    //      stopping_ || wakeSeq_ != snapshot`. Any submit anywhere
    //      causes the predicate to evaluate true on the next wake.
    //   2. `parkedCount_` is incremented around the park bracket.
    //      `submit()` reads it (acquire) to gate the helper notify
    //      (below). When zero (all workers busy), no extra notify
    //      fires — the steady-state hot path pays zero fan-out.
    //   3. Helper notify at `(target + 1) % n`. When `parkedCount_ > 0`,
    //      `submit()` does one additional `notify_one` after the
    //      target notify. Over a wave of N submits this helper
    //      rotates through every worker, guaranteeing that any
    //      parked worker reachable from the wave gets at least one
    //      notify whose predicate (via `wakeSeq_`) will then exit
    //      `cv.wait`, scan + trySteal, and either run the work or
    //      re-park with a fresh snapshot.
    //
    // Why not a single global wake futex (semaphore, shared CV): a
    // single futex shared across all workers contends badly at high
    // pool sizes — sys time blew up from 3 s to 50-170+ s on the
    // rpg_demo `--stress` benchmark at 71 workers. Per-worker CVs
    // spread the contention; one extra `notify_one` per submit is a
    // few nanoseconds on an idle CV.
    //
    // Why not N-wide fan-out: notifying every parked worker on every
    // submit also regressed sys time (49 s in the conditional-fan-out
    // form). The helper-only design keeps the per-submit cost flat
    // across worker count.
    //
    // Pinned by `tests/job_system_missed_wakeup_test.cpp`. Don't
    // remove `wakeSeq_` from the predicate — workers will re-park on
    // any non-direct-queue wake and strand sibling work.

    // Monotonic submit counter — bumped on every successful submit.
    // Workers snapshot before parking; predicate exits when it
    // advances. Release/acquire ordering ensures the snapshot
    // synchronizes with the corresponding `submit()`'s queue push.
    std::atomic<std::uint64_t> wakeSeq_{0};

    // Number of workers currently parked on their own `cv`. Maintained
    // by workers around the park / wake bracket. `submit()` reads this
    // to decide whether the post-target-notify fan-out is needed: when
    // zero, all workers are busy and will pick up new work via their
    // own queue pop on the next loop iteration, so the fan-out would
    // just be cache-line churn. The race with a worker mid-transition
    // is closed by the `wakeSeq_` snapshot — see the `wakeSeq_`
    // comment above.
    std::atomic<std::uint32_t> parkedCount_{0};

    // Round-robin selector for submit-side distribution. Relaxed is fine
    // — we only need uniqueness modulo workerCount, not strict ordering.
    std::atomic<std::uint32_t> pushCounter_{0};

    std::atomic<std::uint32_t> outstanding_{0};  // queued + in-flight
    std::atomic<bool>          stopping_{false};

    // Lifetime job-submission count. Mirrors what callers see on
    // EngineStats::totalJobsSubmitted but covers raw submit() too.
    std::atomic<std::uint64_t> totalJobs_{0};

    // waitIdle synchronization. Decoupled from per-worker queues so that
    // worker pushes and pops don't contend with sim-thread waitIdle.
    std::mutex              doneMtx_;
    std::condition_variable doneCv_;
};

} // namespace threadmaxx::internal
