#include "threadmaxx_navmesh/query.hpp"

#include "threadmaxx_navmesh/detail/solver.hpp"
#include "threadmaxx_navmesh/mesh.hpp"

#include "threadmaxx/Components.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace threadmaxx::navmesh {

struct PathQueryService::Impl {
    const NavMeshRegistry& reg;
    PathQueryServiceConfig cfg;

    mutable std::mutex mtx;
    std::condition_variable cv;

    std::deque<detail::PreparedRequest> queue;
    std::unordered_map<PathId, PathResult> results;
    std::unordered_set<PathId> cancelled;
    // Ids that have been popped off the queue and are currently being
    // solved by a worker. `clear()` needs to tombstone these because
    // they're no longer in `queue` but their result hasn't landed.
    std::unordered_set<PathId> inFlight;
    PathId nextId{1};
    bool shutdown{false};

    // Synchronous-mode scratch (workerThreads == 0). Lives on the
    // calling thread so we don't pay condvar overhead in tests that
    // want the v0.x behavior.
    detail::SolverScratch syncScratch;

    std::vector<std::thread> workers;

    explicit Impl(const NavMeshRegistry& r, PathQueryServiceConfig c)
        : reg(r), cfg(c) {}
};

PathQueryService::PathQueryService(const NavMeshRegistry& registry,
                                   Config cfg)
    : impl_(std::make_unique<Impl>(registry, cfg)) {
    impl_->workers.reserve(cfg.workerThreads);
    for (std::uint32_t i = 0; i < cfg.workerThreads; ++i) {
        impl_->workers.emplace_back([this]() {
            detail::SolverScratch scratch;
            std::unique_lock<std::mutex> g(impl_->mtx);
            while (true) {
                impl_->cv.wait(g, [this]() {
                    return impl_->shutdown || !impl_->queue.empty();
                });
                if (impl_->shutdown && impl_->queue.empty()) return;

                detail::PreparedRequest prep =
                    std::move(impl_->queue.front());
                impl_->queue.pop_front();

                // Cancel-while-pending: skip the solve entirely. The
                // cancelled set acts as a tombstone — if cancel() was
                // called before we popped, we drop the id and move on.
                if (impl_->cancelled.erase(prep.id) > 0) {
                    impl_->cv.notify_all();
                    continue;
                }

                const NavMesh* mesh = impl_->reg.find(prep.req.mesh);
                if (!mesh) {
                    // Mesh was unloaded between request() and solve.
                    // Drop silently — waiters time out.
                    impl_->cv.notify_all();
                    continue;
                }

                impl_->inFlight.insert(prep.id);
                g.unlock();
                PathResult result;
                detail::solvePrepared(*mesh, prep, scratch, result);
                g.lock();
                impl_->inFlight.erase(prep.id);

                // Cancel-during-solve OR clear(): discard the result.
                if (impl_->cancelled.erase(prep.id) > 0) {
                    impl_->cv.notify_all();
                    continue;
                }
                impl_->results.emplace(prep.id, std::move(result));
                impl_->cv.notify_all();
            }
        });
    }
}

PathQueryService::~PathQueryService() {
    if (!impl_) return;
    {
        std::lock_guard<std::mutex> g(impl_->mtx);
        impl_->shutdown = true;
    }
    impl_->cv.notify_all();
    for (auto& t : impl_->workers) {
        if (t.joinable()) t.join();
    }
}

PathId PathQueryService::request(const PathRequest& req) {
    lastStatus_ = PathRequestStatus::Accepted;

    const NavMesh* mesh = impl_->reg.find(req.mesh);
    if (!mesh) {
        lastStatus_ = PathRequestStatus::InvalidMesh;
        return 0;
    }
    const auto startLoc = detail::locate(*mesh, req.start);
    if (!startLoc) {
        lastStatus_ = PathRequestStatus::StartNotOnMesh;
        return 0;
    }
    const auto goalLoc = detail::locate(*mesh, req.goal);
    if (!goalLoc) {
        lastStatus_ = PathRequestStatus::GoalNotOnMesh;
        return 0;
    }

    detail::PreparedRequest prep;
    prep.req = req;
    prep.startLoc = *startLoc;
    prep.goalLoc = *goalLoc;

    // Synchronous mode: solve on the caller thread and store the result
    // directly. Matches v0.x behavior exactly when workerThreads == 0.
    if (impl_->cfg.workerThreads == 0) {
        PathId id;
        {
            std::lock_guard<std::mutex> g(impl_->mtx);
            id = impl_->nextId++;
        }
        prep.id = id;
        PathResult result;
        detail::solvePrepared(*mesh, prep, impl_->syncScratch, result);
        {
            std::lock_guard<std::mutex> g(impl_->mtx);
            impl_->results.emplace(id, std::move(result));
            impl_->cv.notify_all();
        }
        return id;
    }

    PathId id;
    {
        std::lock_guard<std::mutex> g(impl_->mtx);
        id = impl_->nextId++;
        prep.id = id;
        impl_->queue.push_back(std::move(prep));
    }
    impl_->cv.notify_all();
    return id;
}

std::optional<PathResult> PathQueryService::tryGet(PathId id) const {
    std::lock_guard<std::mutex> g(impl_->mtx);
    const auto it = impl_->results.find(id);
    if (it == impl_->results.end()) return std::nullopt;
    return it->second;
}

std::optional<PathResult> PathQueryService::wait(
    PathId id, std::chrono::milliseconds timeout) const {
    std::unique_lock<std::mutex> g(impl_->mtx);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        const auto it = impl_->results.find(id);
        if (it != impl_->results.end()) return it->second;
        // Already cancelled — no result will ever arrive.
        if (impl_->cancelled.count(id) > 0) return std::nullopt;
        if (impl_->shutdown) return std::nullopt;
        if (impl_->cv.wait_until(g, deadline) == std::cv_status::timeout) {
            return std::nullopt;
        }
    }
}

void PathQueryService::cancel(PathId id) {
    {
        std::lock_guard<std::mutex> g(impl_->mtx);
        // If the result is already stored, drop it and stop — no
        // tombstone needed because there's nothing in flight.
        if (impl_->results.erase(id) > 0) {
            impl_->cv.notify_all();
            return;
        }
        // Otherwise mark the id as cancelled so the worker drops it on
        // pop OR on store. The worker erases the entry on consumption.
        impl_->cancelled.insert(id);
    }
    impl_->cv.notify_all();
}

void PathQueryService::clear() {
    {
        std::lock_guard<std::mutex> g(impl_->mtx);
        // Every queued id becomes a tombstone so workers that pop them
        // skip the solve.
        for (const auto& p : impl_->queue) {
            impl_->cancelled.insert(p.id);
        }
        impl_->queue.clear();
        // In-flight ids get tombstoned too — when the worker re-acquires
        // the lock after solve, it sees `cancelled.contains(id)` and
        // drops the result.
        for (PathId id : impl_->inFlight) {
            impl_->cancelled.insert(id);
        }
        // Every ready id is dropped outright.
        for (const auto& kv : impl_->results) {
            // Tombstone so `wait()` returns nullopt fast instead of
            // sitting until timeout.
            impl_->cancelled.insert(kv.first);
        }
        impl_->results.clear();
    }
    impl_->cv.notify_all();
}

std::size_t PathQueryService::storedCount() const noexcept {
    std::lock_guard<std::mutex> g(impl_->mtx);
    return impl_->results.size();
}

std::size_t PathQueryService::pendingCount() const noexcept {
    std::lock_guard<std::mutex> g(impl_->mtx);
    return impl_->queue.size();
}

std::uint32_t PathQueryService::workerCount() const noexcept {
    return impl_->cfg.workerThreads;
}

} // namespace threadmaxx::navmesh
