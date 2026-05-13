#pragma once

#include <atomic>
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
    // a worker's local queue (round-robin by submit count).
    void submit(JobFn fn);

    // Block until every submitted job has finished. Safe to call again.
    void waitIdle();

    std::uint32_t workerCount() const noexcept {
        return static_cast<std::uint32_t>(workers_.size());
    }

private:
    struct Worker {
        std::deque<JobFn>       queue;
        std::mutex              mtx;
        std::condition_variable cv;
        std::thread             thread;
    };

    void workerLoop(std::uint32_t selfIdx);
    // Tries to take a job from one of the other workers' queues. Uses
    // try_lock to avoid blocking on producers and skips queues that are
    // currently being pushed to.
    JobFn trySteal(std::uint32_t selfIdx) noexcept;

    std::vector<std::unique_ptr<Worker>> workers_;

    // Round-robin selector for submit-side distribution. Relaxed is fine
    // — we only need uniqueness modulo workerCount, not strict ordering.
    std::atomic<std::uint32_t> pushCounter_{0};

    std::atomic<std::uint32_t> outstanding_{0};  // queued + in-flight
    std::atomic<bool>          stopping_{false};

    // waitIdle synchronization. Decoupled from per-worker queues so that
    // worker pushes and pops don't contend with sim-thread waitIdle.
    std::mutex              doneMtx_;
    std::condition_variable doneCv_;
};

} // namespace threadmaxx::internal
