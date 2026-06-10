#pragma once

#include "threadmaxx_navmesh/query.hpp"

#include <cstdint>
#include <memory>
#include <vector>

/// Batch path solver (N6) — fan a list of `PathRequest` instances out
/// over an internal worker pool and return a 1:1 result list. Built on
/// top of the same A* + funnel pipeline as `PathQueryService`; the
/// difference is that callers hand over the whole batch up front, the
/// solver blocks on a single barrier instead of N independent waits.
namespace threadmaxx::navmesh {

class NavMeshRegistry;

/// Input to `BatchPathSolver::solve`. Each entry is solved independently;
/// the result vector is written in the same order as `requests`.
struct BatchPathRequest {
    std::vector<PathRequest> requests;
};

/// Output of `BatchPathSolver::solve`. `results[i]` corresponds to
/// `BatchPathRequest::requests[i]`. Failures appear as `success == false`
/// rather than missing entries — the index alignment is the contract.
struct BatchPathResult {
    std::vector<PathResult> results;
};

/// Tunables for the solver pool. `workerThreads = 0` runs every solve
/// on the caller thread (no pool spawn — useful for tests / fallbacks).
struct BatchPathSolverConfig {
    std::uint32_t workerThreads{1};
};

/// Owns a persistent worker pool that drains a single batch at a time.
/// The pool stays alive between calls — `solve()` is the public entry
/// point. The calling thread participates in the work, so the effective
/// parallelism is `workerThreads + 1`.
class BatchPathSolver {
public:
    using Config = BatchPathSolverConfig;

    /// `registry` must outlive the solver — only borrowed.
    explicit BatchPathSolver(const NavMeshRegistry& registry,
                             Config cfg = {});
    ~BatchPathSolver();
    BatchPathSolver(const BatchPathSolver&) = delete;
    BatchPathSolver& operator=(const BatchPathSolver&) = delete;
    // Non-movable: worker threads + condvars don't relocate cleanly and
    // we never move live solvers in practice.
    BatchPathSolver(BatchPathSolver&&) = delete;
    BatchPathSolver& operator=(BatchPathSolver&&) = delete;

    /// Solve `batch`. Blocks until every entry has a result. `results`
    /// is sized to match `batch.requests.size()`; entries that failed
    /// pre-solve (invalid mesh / start / goal off-mesh) are reported as
    /// `success == false` with `ready == true`. Safe to call from a
    /// single producer thread; concurrent `solve()` calls on the same
    /// instance are undefined behavior.
    BatchPathResult solve(const BatchPathRequest& batch);

    /// Configured worker thread count. `0` means everything runs on the
    /// caller thread.
    std::uint32_t workerCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace threadmaxx::navmesh
