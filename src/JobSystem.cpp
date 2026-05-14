#include "JobSystem.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace threadmaxx::internal {

JobSystem::JobSystem(std::uint32_t workerCount) {
    if (workerCount == 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        workerCount = std::max(1u, hw > 1 ? hw - 1 : 1u);
    }
    workers_.reserve(workerCount);
    for (std::uint32_t i = 0; i < workerCount; ++i) {
        workers_.emplace_back(std::make_unique<Worker>());
    }
    // Spawn threads after every Worker is constructed so workerLoop can
    // freely index into workers_ for stealing without a TOCTOU race.
    for (std::uint32_t i = 0; i < workerCount; ++i) {
        workers_[i]->thread = std::thread([this, i] { workerLoop(i); });
    }
}

JobSystem::~JobSystem() {
    waitIdle();
    stopping_.store(true, std::memory_order_release);
    // Take and release each worker's mutex so the stopping_ store is
    // visible to a worker that is about to re-evaluate its wait predicate.
    // notify_all then wakes any worker currently parked.
    for (auto& w : workers_) {
        { std::lock_guard<std::mutex> lk(w->mtx); }
        w->cv.notify_all();
    }
    for (auto& w : workers_) {
        if (w->thread.joinable()) w->thread.join();
    }
}

void JobSystem::submit(JobFn fn, JobPriority priority) {
    outstanding_.fetch_add(1, std::memory_order_acq_rel);
    totalJobs_.fetch_add(1, std::memory_order_relaxed);
    const std::uint32_t n = static_cast<std::uint32_t>(workers_.size());
    const std::uint32_t idx = pushCounter_.fetch_add(1, std::memory_order_relaxed) % n;
    auto& w = *workers_[idx];
    const std::size_t pri = static_cast<std::size_t>(priority);
    {
        std::lock_guard<std::mutex> lk(w.mtx);
        w.queues[pri].emplace_back(std::move(fn));
    }
    w.cv.notify_one();
}

JobSystemStats JobSystem::stats() const noexcept {
    JobSystemStats s;
    s.totalJobs = totalJobs_.load(std::memory_order_relaxed);
    s.workerCount = static_cast<std::uint32_t>(workers_.size());
    // Each Worker mutates its own ownPops/stolenJobs/histogram only
    // from its own thread. Reads from elsewhere see a recent value (a
    // relaxed view is sufficient for stats counters).
    for (const auto& w : workers_) {
        s.ownPops    += w->ownPops;
        s.stolenJobs += w->stolenJobs;
        for (std::size_t i = 0; i < kJobDurationHistogramBins; ++i) {
            s.jobDurationHistogram[i] += w->histogram[i];
        }
    }
    return s;
}

std::size_t JobSystem::binFor(std::chrono::nanoseconds duration) noexcept {
    // Convert to microseconds, floor-divided. Bins are log2-spaced:
    //   bin i covers [2^i, 2^(i+1)) µs for i < (N-1); the last bin
    //   catches everything beyond. Sub-microsecond jobs land in bin 0.
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                        duration).count();
    if (us < 2) return 0;
    // Find the highest set bit of `us` — i.e. floor(log2(us)).
    std::uint64_t v = static_cast<std::uint64_t>(us);
    std::size_t bin = 0;
    while (v > 1) { v >>= 1; ++bin; }
    if (bin >= kJobDurationHistogramBins) bin = kJobDurationHistogramBins - 1;
    return bin;
}

void JobSystem::waitIdle() {
    std::unique_lock<std::mutex> lk(doneMtx_);
    doneCv_.wait(lk, [this] {
        return outstanding_.load(std::memory_order_acquire) == 0;
    });
}

JobSystem::JobFn JobSystem::trySteal(std::uint32_t selfIdx) noexcept {
    const std::uint32_t n = static_cast<std::uint32_t>(workers_.size());
    // Visit siblings in rotation. try_lock skips victims whose producer is
    // currently pushing, so a busy submit thread doesn't block stealers.
    for (std::uint32_t i = 1; i < n; ++i) {
        auto& victim = *workers_[(selfIdx + i) % n];
        if (!victim.mtx.try_lock()) continue;
        std::lock_guard<std::mutex> lk(victim.mtx, std::adopt_lock);
        // §3.5 batch 12: prefer higher-priority deques when stealing.
        // We still steal from the back (oldest) of each deque to keep
        // the producer-consumer contention pattern unchanged from
        // batch 1's design.
        for (std::size_t pri = 0; pri < kJobPriorityLevels; ++pri) {
            auto& q = victim.queues[pri];
            if (q.empty()) continue;
            JobFn job = std::move(q.back());
            q.pop_back();
            workers_[selfIdx]->stolenJobs++;
            return job;
        }
    }
    return nullptr;
}

void JobSystem::workerLoop(std::uint32_t selfIdx) {
    auto& self = *workers_[selfIdx];
    // §3.5 batch 12: scan deques in priority order (High → Normal → Low),
    // popping from the front (FIFO within a priority class) to keep
    // batch-submitted work executing in submission order at each level.
    auto popOwn = [&](JobFn& out) {
        for (std::size_t pri = 0; pri < kJobPriorityLevels; ++pri) {
            auto& q = self.queues[pri];
            if (!q.empty()) {
                out = std::move(q.front());
                q.pop_front();
                return true;
            }
        }
        return false;
    };
    for (;;) {
        JobFn job;

        bool fromOwn = false;
        // 1) Drain own queues first, priority-ordered.
        {
            std::lock_guard<std::mutex> lk(self.mtx);
            if (popOwn(job)) fromOwn = true;
        }

        // 2) If own queues are empty, try stealing from siblings.
        if (!job) job = trySteal(selfIdx);

        // 3) Still nothing — park until the producer notifies us or we are
        //    asked to stop.
        if (!job) {
            std::unique_lock<std::mutex> lk(self.mtx);
            self.cv.wait(lk, [this, &self] {
                return self.hasWork() ||
                       stopping_.load(std::memory_order_acquire);
            });
            if (stopping_.load(std::memory_order_acquire) && !self.hasWork()) {
                return;
            }
            if (popOwn(job)) fromOwn = true;
        }

        if (fromOwn) self.ownPops++;

        if (job) {
            const auto t0 = std::chrono::steady_clock::now();
            job();
            const auto t1 = std::chrono::steady_clock::now();
            self.histogram[binFor(t1 - t0)]++;
            if (outstanding_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                // Last in-flight job for the batch. Acquiring doneMtx_
                // here synchronizes with waitIdle's predicate check.
                std::lock_guard<std::mutex> lk(doneMtx_);
                doneCv_.notify_all();
            }
        }
    }
}

} // namespace threadmaxx::internal
