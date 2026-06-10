#include "threadmaxx_navmesh/crowd.hpp"

#include "threadmaxx_navmesh/detail/solver.hpp"
#include "threadmaxx_navmesh/mesh.hpp"
#include "threadmaxx_navmesh/query.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace threadmaxx::navmesh {

namespace {

/// Solve a single request into `out`. Pre-validation (mesh lookup +
/// start/goal locate) lives here so per-request failures degrade to
/// `success == false` rather than a fatal error for the batch. `index`
/// is folded into `out.id` so callers can correlate by id if they
/// don't keep the input vector around.
void solveOne(const NavMeshRegistry& reg,
              const PathRequest& req,
              detail::SolverScratch& scratch,
              PathResult& out,
              std::size_t index) {
    out = PathResult{};
    const PathId pseudoId = static_cast<PathId>(index + 1u);
    out.id = pseudoId;
    out.ready = true;

    const NavMesh* mesh = reg.find(req.mesh);
    if (!mesh) return;

    auto sloc = detail::locate(*mesh, req.start);
    if (!sloc) return;
    auto gloc = detail::locate(*mesh, req.goal);
    if (!gloc) return;

    detail::PreparedRequest prep;
    prep.id = pseudoId;
    prep.req = req;
    prep.startLoc = *sloc;
    prep.goalLoc = *gloc;
    detail::solvePrepared(*mesh, prep, scratch, out);
}

} // namespace

struct BatchPathSolver::Impl {
    const NavMeshRegistry& reg;
    Config cfg;

    std::mutex mtx;
    // Workers wait on `cv` for a new batch generation; the producer
    // waits on `doneCv` for all workers to bump `doneCount`.
    std::condition_variable cv;
    std::condition_variable doneCv;

    // Per-batch state — `solve()` populates under `mtx`, then notifies
    // `cv`. The pointers live on the producer's stack and stay alive
    // until `solve()` returns (which only happens after `doneCount`
    // reaches `cfg.workerThreads`).
    const BatchPathRequest* current{nullptr};
    std::vector<PathResult>* results{nullptr};
    std::size_t total{0};
    std::atomic<std::size_t> nextIndex{0};
    std::atomic<std::uint32_t> doneCount{0};
    std::uint64_t batchGen{0};

    bool shutdown{false};
    std::vector<std::thread> workers;

    explicit Impl(const NavMeshRegistry& r, Config c) : reg(r), cfg(c) {}
};

BatchPathSolver::BatchPathSolver(const NavMeshRegistry& registry, Config cfg)
    : impl_(std::make_unique<Impl>(registry, cfg)) {
    impl_->workers.reserve(cfg.workerThreads);
    for (std::uint32_t w = 0; w < cfg.workerThreads; ++w) {
        impl_->workers.emplace_back([this]() {
            detail::SolverScratch scratch;
            std::uint64_t seenGen = 0;
            while (true) {
                const BatchPathRequest* batch = nullptr;
                std::vector<PathResult>* outVec = nullptr;
                std::size_t total = 0;
                {
                    std::unique_lock<std::mutex> g(impl_->mtx);
                    impl_->cv.wait(g, [this, seenGen]() {
                        return impl_->shutdown ||
                               impl_->batchGen != seenGen;
                    });
                    if (impl_->shutdown) return;
                    seenGen = impl_->batchGen;
                    batch = impl_->current;
                    outVec = impl_->results;
                    total = impl_->total;
                }

                if (batch && outVec) {
                    while (true) {
                        const std::size_t idx =
                            impl_->nextIndex.fetch_add(
                                1, std::memory_order_relaxed);
                        if (idx >= total) break;
                        solveOne(impl_->reg, batch->requests[idx], scratch,
                                 (*outVec)[idx], idx);
                    }
                }

                // Bump done under the lock so the producer's `doneCv`
                // wait sees a coherent count when it wakes.
                {
                    std::lock_guard<std::mutex> g(impl_->mtx);
                    impl_->doneCount.fetch_add(
                        1, std::memory_order_acq_rel);
                }
                impl_->doneCv.notify_all();
            }
        });
    }
}

BatchPathSolver::~BatchPathSolver() {
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

BatchPathResult BatchPathSolver::solve(const BatchPathRequest& batch) {
    BatchPathResult out;
    out.results.assign(batch.requests.size(), PathResult{});

    if (batch.requests.empty()) return out;

    // Synchronous mode: no pool, no signalling — just solve each on
    // the caller thread. Keeps tests + small-batch callers cheap.
    if (impl_->cfg.workerThreads == 0) {
        detail::SolverScratch scratch;
        for (std::size_t i = 0; i < batch.requests.size(); ++i) {
            solveOne(impl_->reg, batch.requests[i], scratch,
                     out.results[i], i);
        }
        return out;
    }

    // Publish the batch + reset counters under the lock so workers
    // observe a consistent snapshot when their CV wait returns.
    {
        std::lock_guard<std::mutex> g(impl_->mtx);
        impl_->current = &batch;
        impl_->results = &out.results;
        impl_->total = batch.requests.size();
        impl_->nextIndex.store(0, std::memory_order_relaxed);
        impl_->doneCount.store(0, std::memory_order_relaxed);
        ++impl_->batchGen;
    }
    impl_->cv.notify_all();

    // Producer participates in the work loop. Effective parallelism is
    // `workerThreads + 1`. If the producer finishes before workers wake
    // up they'll exit the inner loop on the first `fetch_add` and bump
    // `doneCount` immediately — no special case needed.
    detail::SolverScratch producerScratch;
    while (true) {
        const std::size_t idx =
            impl_->nextIndex.fetch_add(1, std::memory_order_relaxed);
        if (idx >= impl_->total) break;
        solveOne(impl_->reg, batch.requests[idx], producerScratch,
                 out.results[idx], idx);
    }

    // Wait for every worker to bump `doneCount` for this batch.
    {
        std::unique_lock<std::mutex> g(impl_->mtx);
        impl_->doneCv.wait(g, [this]() {
            return impl_->doneCount.load(std::memory_order_acquire) >=
                   impl_->cfg.workerThreads;
        });
        // Clear the pointers so a stale dereference would be a nullptr
        // crash rather than a silent dangling-pointer read.
        impl_->current = nullptr;
        impl_->results = nullptr;
        impl_->total = 0;
    }

    return out;
}

std::uint32_t BatchPathSolver::workerCount() const noexcept {
    return impl_->cfg.workerThreads;
}

} // namespace threadmaxx::navmesh
