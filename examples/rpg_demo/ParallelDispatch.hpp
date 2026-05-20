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

/// Dispatch `fn(Range, CommandBuffer&)` over `[0, count)` either
/// inline (if `count` is small) or via `ctx.parallelFor(count, 0)`.
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
        ctx.parallelFor(count, /*grain*/ 0, std::forward<Fn>(fn));
    }
}

} // namespace rpg
