#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace threadmaxx::internal {

// Fixed-size worker pool with a FIFO queue. Designed for "submit a batch of
// jobs, wait for the whole batch" — which is exactly the shape of our
// per-system parallel-for pattern.
//
// Determinism notes: with N workers, the *execution order* of jobs in a
// batch is not deterministic. Determinism is enforced at the commit phase,
// which applies command buffers strictly in submission order. So workers
// can race freely without affecting the final world state.
class JobSystem {
public:
    using JobFn = std::function<void()>;

    // workerCount=0 means "pick a sensible default".
    explicit JobSystem(std::uint32_t workerCount);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // Submit a job. Increments the outstanding-job counter.
    void submit(JobFn fn);

    // Block until every submitted job has finished. Safe to call again.
    void waitIdle();

    std::uint32_t workerCount() const noexcept { return static_cast<std::uint32_t>(workers_.size()); }

private:
    void workerLoop();

    std::vector<std::thread>  workers_;
    std::deque<JobFn>         queue_;

    std::mutex                queueMutex_;
    std::condition_variable   queueCv_;     // workers wait here for jobs
    std::condition_variable   doneCv_;      // submitters wait here for idle

    std::atomic<std::uint32_t> outstanding_{0};  // queued + in-flight
    std::atomic<bool>          stopping_{false};
};

} // namespace threadmaxx::internal
