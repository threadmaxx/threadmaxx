// 2026-05-20 — small parallel-dispatch helper for the rpg_demo.
//
// Wraps `ctx.parallelFor` with a fallback to `ctx.single` when the
// work count is too small to spread across the worker pool. The
// engine's `parallelFor` accepts any positive count, but with
// `grain=0` and `count < workerCount` it submits only `count`
// sub-jobs round-robin to `count` workers. The OTHER (workerCount -
// count) workers stay parked in `cv.wait`; the lone submitting
// worker can't make progress because its outer wave latch hasn't
// fired yet — that other worker IS in the wave's sim-thread tail or
// some other job-blocked state. With insufficient sub-jobs to
// notify all the workers that need to drain their wave-system jobs,
// you can hit a nested-parallelism deadlock.
//
// The fix: when `count <= kInlineLimit` (~ 4×worker pool size we
// might see on CI hardware), bypass the JobSystem entirely and run
// the body inline on the calling thread via `ctx.single()` — which
// just creates a CommandBuffer and invokes the lambda. Zero
// parallelism for that call, but no deadlock and no worker-pool
// notification dance for trivially-small batches.
//
// 2026-05-23 — ADAPTIVE_TUNING.md T1: parallel path now also clamps
// fan-out so each sub-job carries at least `kMinRowsPerWorker` rows.
// On big boxes (workerCount=71) with modest counts (~40k), the D12
// audit showed 71 sub-jobs of ~560 entities each (~17 µs/sub-job)
// lose to dispatch overhead. The clamp keeps sub-job duration above
// the profitable floor without changing the engine's default grain
// picker for other call sites.
//
// Used by MovementSystem / CubeRenderSystem / NPCBrainSystem.

#pragma once

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/System.hpp>

#include <cstdint>
#include <utility>

namespace rpg {

/// Inline threshold. Picked safely above any realistic per-tick
/// workerCount in the rpg_demo (the executable uses
/// `hardware_concurrency - 1` so up to ~256 on huge boxes; the test
/// harness uses 4). 256 chosen so even on a 64-core box the inline
/// path only fires for genuinely tiny batches.
constexpr std::uint32_t kInlineDispatchLimit = 256;

/// ADAPTIVE_TUNING.md T1 — minimum rows per sub-job. Below this, the
/// JobLatch + cv-wakeup overhead can exceed the sub-job's own work.
/// 256 chosen conservatively: at ~25 ns/row (cube-render's regime
/// after the D12 fixes) that's ~6 µs of work per sub-job, well above
/// the few-microsecond dispatch floor on a 70+ worker box. For the
/// rpg_demo's current ~140k-entity cube-render workload this floor
/// does NOT bind — the engine's B28 4×workers slack still wins for
/// load balancing. The floor protects small-count `parallelFor`
/// callers (e.g. NPC brain) where 4×workers sub-jobs would be 70+
/// sub-jobs of single-digit rows each. Tuning candidate; T5's
/// adaptive policy can override on a per-system basis.
constexpr std::uint32_t kMinRowsPerWorker = 256;

/// ADAPTIVE_TUNING.md T1 — sub-job count target. Mirrors the engine's
/// `pickGrain` heuristic (B28: `workerCount * 4`) so steady-state
/// behavior matches what the engine would have picked with grain=0.
constexpr std::uint32_t kLoadBalanceMultiplier = 4u;

/// ADAPTIVE_TUNING.md T1 — given a work count and the pool size,
/// pick the sub-job count: the smaller of (a) B28's load-balancing
/// target `workers * 4`, and (b) the floor `count / kMinRowsPerWorker`
/// that keeps each sub-job above the dispatch overhead. Returns ≥ 1
/// for non-zero counts. Constexpr so the unit test can exercise it
/// without an engine.
constexpr std::uint32_t effectiveSubJobCount(std::uint32_t count,
                                             std::uint32_t workerCount) noexcept {
    if (count == 0 || workerCount == 0) return 1u;
    const std::uint32_t loadBalanceTarget = workerCount * kLoadBalanceMultiplier;
    const std::uint32_t minRowsBound      = count / kMinRowsPerWorker;
    const std::uint32_t bounded =
        (minRowsBound < 1u) ? 1u : minRowsBound;
    return (loadBalanceTarget < bounded) ? loadBalanceTarget : bounded;
}

/// Dispatch `fn(Range, CommandBuffer&)` over `[0, count)` either
/// inline (if `count` is small) or via `ctx.parallelFor(count, ...)`
/// with a grain chosen so the resulting sub-job count is
/// `effectiveSubJobCount(count, ctx.workerCount())`.
/// `fn` must be invocable with `(threadmaxx::Range, threadmaxx::CommandBuffer&)`.
template <typename Fn>
inline void dispatchOrInline(threadmaxx::SystemContext& ctx,
                             std::uint32_t count,
                             Fn&& fn) {
    if (count == 0) return;
    if (count <= kInlineDispatchLimit) {
        // ctx.single() runs the lambda inline on whatever thread
        // called this update (sim or worker). The Range argument
        // it passes is {0,0} — ignore it; we supply our own.
        ctx.single([count, fn = std::forward<Fn>(fn)]
                   (threadmaxx::Range, threadmaxx::CommandBuffer& cb) mutable {
            fn(threadmaxx::Range{0, count}, cb);
        });
    } else {
        const std::uint32_t subJobs =
            effectiveSubJobCount(count, ctx.workerCount());
        // ceil(count/subJobs) — the last sub-job may carry a partial
        // chunk; truncating would silently dispatch one extra job.
        const std::uint32_t grain = (count + subJobs - 1u) / subJobs;
        ctx.parallelFor(count, grain, std::forward<Fn>(fn));
    }
}

} // namespace rpg
