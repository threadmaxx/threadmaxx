#pragma once

#include "threadmaxx_animation/eval.hpp"
#include "threadmaxx_animation/pose.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

/// Crowd-evaluation helpers (A7). Internal detail: range-scoped batch
/// evaluation for engine `forEachChunk` integration, and a LIFO
/// `PoseBufferPool` that lets a worker reuse a small set of pose
/// buffers across many agents without per-tick allocation.
///
/// Public batch entry point is `threadmaxx::animation::evaluateBatch`
/// in `eval.hpp`; this header is the detail-namespace surface for game
/// code that wants finer control (per-range slicing, per-worker pools).
namespace threadmaxx::animation::detail {

/// Evaluate `animators[i]` into `outPoses[i]` for `i in [begin, end)`.
/// Same contract as `evaluateBatch`, restricted to a half-open range.
/// Used by engine integrations to feed `forEachChunk`'s
/// `(spanBegin, spanEnd)` row partitions directly.
///
/// `outResults` / `skipMask` follow the same nullable semantics as
/// `evaluateBatch`: if non-empty, sized to match `animators`; if empty,
/// per-agent results are discarded and no agents are skipped.
inline void evaluateBatchRange(std::span<Animator> animators,
                               std::span<PoseBuffer> outPoses,
                               std::size_t begin,
                               std::size_t end,
                               EvalContext ctx,
                               std::span<EvalResult> outResults = {},
                               std::span<const std::uint8_t> skipMask = {}) {
    const bool haveResults = !outResults.empty();
    const bool haveSkip = !skipMask.empty();
    for (std::size_t i = begin; i < end; ++i) {
        if (haveSkip && skipMask[i] != 0) continue;
        EvalResult r = animators[i].evaluate(ctx, outPoses[i]);
        if (haveResults) outResults[i] = std::move(r);
    }
}

/// LIFO pool of reusable `PoseBuffer`s. One pool per worker is the
/// expected usage — `acquire(jointCount)` borrows a buffer (resizing if
/// needed); `release(buf)` returns it. After warm-up the pool's
/// capacity stabilizes at the worker's peak nesting depth and acquire
/// is allocation-free.
///
/// Not thread-safe: callers MUST partition pool instances per-worker.
/// The engine integration pattern is to keep a `std::vector<PoseBufferPool>`
/// sized to `engine.workerCount()` and index by worker id.
class PoseBufferPool {
public:
    PoseBufferPool() = default;

    /// Pre-grow the pool so the first `count` acquires hit the steady
    /// state immediately. Each buffer is sized to `jointCount`.
    void reserve(std::size_t count, std::size_t jointCount) {
        pool_.reserve(pool_.size() + count);
        for (std::size_t i = 0; i < count; ++i) {
            PoseBuffer buf;
            buf.resize(jointCount);
            pool_.push_back(std::move(buf));
        }
    }

    /// Borrow a buffer. If the pool is empty, allocates a fresh one.
    /// Otherwise pops the most-recently-released buffer (LIFO) and
    /// resizes it to `jointCount` — the resize is a no-op when the pool
    /// has steady-state capacity.
    PoseBuffer acquire(std::size_t jointCount) {
        if (pool_.empty()) {
            PoseBuffer buf;
            buf.resize(jointCount);
            return buf;
        }
        PoseBuffer buf = std::move(pool_.back());
        pool_.pop_back();
        buf.resize(jointCount);
        return buf;
    }

    /// Return a borrowed buffer to the pool. Move-only — caller's
    /// reference is invalid after this call.
    void release(PoseBuffer&& buf) noexcept {
        pool_.push_back(std::move(buf));
    }

    /// Current pool size (number of buffers available for `acquire`
    /// without allocation).
    std::size_t size() const noexcept { return pool_.size(); }
    bool empty() const noexcept { return pool_.empty(); }

private:
    std::vector<PoseBuffer> pool_;
};

} // namespace threadmaxx::animation::detail
