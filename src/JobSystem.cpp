#include "JobSystem.hpp"

#include <algorithm>

namespace threadmaxx::internal {

JobSystem::JobSystem(std::uint32_t workerCount) {
    if (workerCount == 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        // Leave one logical core for the simulation/render thread when
        // possible. Fall back to at least one worker.
        workerCount = std::max(1u, hw > 1 ? hw - 1 : 1u);
    }
    workers_.reserve(workerCount);
    for (std::uint32_t i = 0; i < workerCount; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

JobSystem::~JobSystem() {
    // Drain anything still in-flight, then signal workers to exit.
    waitIdle();
    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        stopping_.store(true, std::memory_order_release);
    }
    queueCv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
}

void JobSystem::submit(JobFn fn) {
    {
        std::lock_guard<std::mutex> lk(queueMutex_);
        queue_.emplace_back(std::move(fn));
        outstanding_.fetch_add(1, std::memory_order_release);
    }
    queueCv_.notify_one();
}

void JobSystem::waitIdle() {
    std::unique_lock<std::mutex> lk(queueMutex_);
    doneCv_.wait(lk, [this] {
        return outstanding_.load(std::memory_order_acquire) == 0;
    });
}

void JobSystem::workerLoop() {
    for (;;) {
        JobFn job;
        {
            std::unique_lock<std::mutex> lk(queueMutex_);
            queueCv_.wait(lk, [this] {
                return !queue_.empty() || stopping_.load(std::memory_order_acquire);
            });
            if (queue_.empty()) {
                if (stopping_.load(std::memory_order_acquire)) return;
                continue;
            }
            job = std::move(queue_.front());
            queue_.pop_front();
        }

        job();

        if (outstanding_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // Last in-flight job for the batch — wake anything in waitIdle().
            std::lock_guard<std::mutex> lk(queueMutex_);
            doneCv_.notify_all();
        }
    }
}

} // namespace threadmaxx::internal
