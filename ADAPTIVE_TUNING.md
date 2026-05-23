# Adaptive Tuning — Plan

Phase-T planning doc for the `threadmaxx` adaptive runtime tuner.
Companion to `OPTIMIZATION_PATH.md`; references `FUTURE_WORK.md` §1
(target outcome). Last refreshed 2026-05-23.

## 1. Why we need this

The D12 audit (2026-05-23, commit `9cc1302`) collected hard evidence
that **the engine's "more workers = faster" assumption breaks down
when per-sub-job work shrinks below ~1 ms**:

| Workers | rpg_demo normal mean step | rpg_demo stress mean step |
|---|---|---|
| 4 | 10.4 ms | — |
| **8** | **6.1 ms ★** | 25.5 ms |
| 16 | 8.0 ms | **19.4 ms ★** |
| 32 | 9.3 ms | 23.0 ms |
| 71 (auto: hw_concurrency - 1) | 8.3 ms | **31.2 ms (+66%)** |

The auto default is wildly oversubscribed on big boxes. The
mitigation we shipped — hard-coded `cfg.workerCount = 8 / 16` in
`examples/rpg_demo/main.cpp` — works for the demo but is the wrong
shape for a library: every user has to discover the sweet spot for
their workload, on every machine, on every code change.

Adaptive tuning gives the engine a principled way to land on the
right operating point without per-user benchmark archaeology.

## 2. What "adaptive tuning" should and should not be

We synthesized two sources: the btw discussion's three options (cap
worker count via a runtime hint) and the ChatGPT design note's
broader knob taxonomy. The plan below picks **grain** as the primary
lever and **worker cap** as the secondary lever, both driven by
**per-system sub-job duration telemetry**.

**Will tune:**

- `parallelFor` effective worker fan-out (per call / per system)
- per-system preferred grain (already exists; tuner just writes it)
- skippable-system bias under frame-budget pressure (via existing
  `SkipPolicy::Budget`)

**Will NOT tune** (ChatGPT's verdict, validated by us):

- `Config::workerCount` per tick — too expensive, OS-level concern,
  destabilizes profiling and replay. Set at boot; optionally
  re-evaluated at safe reload points (rare administrative action).
- Memory layout, archetype storage, command ordering, hash rules,
  public API shape — any change that affects `EngineStats::commitHash`
  is forbidden by the determinism contract.
- Worker count via "adaptive resizing." If we need to throttle a
  big pool we do it via fan-out caps, not by spawning / joining
  threads on the hot path.

**Core principles:**

1. **Opt-in.** No tuning unless the host calls `setTuningPolicy(...)`.
2. **Advisory-first.** The policy proposes patches; the engine
   applies them at tick boundaries between waves.
3. **Bounded.** One change per adjustment window, capped step size.
4. **Hysteresis-based.** Moving averages + cooldown timers; never
   chase a single bad frame.
5. **Determinism-safe.** Same input + same scripted policy stream =
   bit-identical `commitHash`. Active mode is for solo / non-replay
   workloads; Scripted mode for networked / replay.
6. **Easy to disable + inspect.** `setTuningPolicy(nullptr)` reverts.
   Every change is logged via `ILogger` and surfaced in
   `FrameSnapshot`.

## 3. Bench + test infrastructure

The tuner is **benchmark-driven**. Every batch ships with a gate
that the bench must improve (or at minimum not regress).

**Primary bench:** `examples/rpg_demo/perf_audit_rpg_demo` (already
shipped in commit `9cc1302`). Headless, links `rpg_demo_core`, dumps
per-system per-tick timings. Drives both normal and stress modes via
`--stress` + `--workers=N`.

**New bench (introduced in T1):** `bench/adaptive_tuning_bench` — a
parameterized harness that runs N ticks against a synthetic workload
(known sub-job duration distributions) so we can validate convergence
without rpg_demo's noise. The synthetic loops include:

- "tiny-fanout": 100k entities, sub-job work ~50 µs (the
  cube-render case)
- "huge-chunk": one chunk of 200k entities, sub-job work ~10 µs
  (B28 sub-job dispatch case)
- "spiky-tail": uneven distribution, one job 10× the others
- "stable": balanced 50 ms wave, baseline / no tuning expected

**Test infrastructure:**

- `tests/adaptive_tuning/` — new directory, follows the
  `tests/Check.hpp` pattern.
- Determinism gate: `test_adaptive_determinism.cpp` runs the
  same workload twice, asserts the policy's patch stream + the
  resulting `commitHash` are byte-identical.
- Convergence gate: `test_adaptive_grain_convergence.cpp` runs the
  tiny-fanout workload for 600 ticks with the built-in policy and
  asserts the chosen grain settles within ±1 step of the optimum
  picked offline.
- Hysteresis gate: `test_adaptive_no_oscillation.cpp` runs the
  spiky-tail workload, injects a deliberate noise burst, asserts
  the policy does not oscillate (no more than one change per
  60-tick window).

## 4. Batches

Six batches, ordered from cheapest+highest-impact to most invasive.
Each batch is shippable independently; later batches build on
earlier ones but the engine remains useful at every cut point.

### Batch T1 — Per-call sub-job floor (userland) — LANDED 2026-05-23

**Scope.** Pure rpg_demo. `ParallelDispatch.hpp::dispatchOrInline`
gains two constants — `kMinRowsPerWorker = 256` (floor for sub-job
work) and `kLoadBalanceMultiplier = 4` (mirroring the engine's B28
auto-grain heuristic) — and a free function `effectiveSubJobCount`:

```cpp
constexpr std::uint32_t effectiveSubJobCount(uint32_t count,
                                             uint32_t workerCount) {
    if (count == 0 || workerCount == 0) return 1u;
    const uint32_t loadBalanceTarget = workerCount * kLoadBalanceMultiplier;
    const uint32_t minRowsBound      = count / kMinRowsPerWorker;
    const uint32_t bounded = std::max(1u, minRowsBound);
    return std::min(loadBalanceTarget, bounded);
}
```

The dispatch then computes `grain = ceil(count / subJobs)` and calls
`ctx.parallelFor(count, grain, fn)`. No engine API change.

**Calibration note (post-implementation).** The initial draft of T1
used `kMinRowsPerWorker = 2000` and `effectiveWorkers = ceil(count /
minRows)` (one sub-job per "effective worker"). That formula defeated
B28's 4× load-balancing slack — cube-render's `wait_ms` jumped from
~0.04 ms baseline to ~0.82 ms, and mean step regressed 7.8 → 9.0 ms.
The fix preserves B28's load-balancing multiplier and uses the floor
ONLY as a cap on candidate sub-job count for small `count` cases.
The unit test exercises both regimes.

**Bench gate result.** `perf_audit_rpg_demo 300 --workers=71` (normal
mode), 5 runs back-to-back: median 8.4 ms (range 7.8–10.0). Baseline
3-run median 8.1 ms (range 7.8–9.1). Within noise. The gate's win
turned out smaller than the plan's "8.3 → ≤ 8 ms" framing because
cube-render at 140k entities × 71 workers already lands at the same
sub-job count under both auto-grain and T1's formula (284 sub-jobs).
T1's actual contribution at this scale is **neutrality** with
auto-grain; its value is the **floor** that prevents pathological
sub-division when called with `count` just above the inline limit.

**Test gate.** `test_dispatch_min_rows_per_worker.cpp` lands;
asserts the formula across (1) edge cases (zero count, zero workers),
(2) single-worker pool, (3) sub-`kMinRows` counts clamp to 1
sub-job, (4) above-floor counts unlock more sub-jobs incrementally,
(5) the crossover where loadBalance and floor agree, (6) cube-render
regime at 140k×71 returns 284, (7) sweep agreement with a reference
implementation. Full rpg_demo suite stays 117/117.

**Risk.** Low. Pure userland heuristic; engine semantics unchanged.

**What it enables.** Establishes the heuristic and the per-call
control point. Provides a known-safe baseline for T2's per-system
cap to extend.

---

### Batch T2 — Engine: `ISystem::preferredWorkerCap()` — LANDED 2026-05-23

**Scope.** New virtual on `ISystem`:

```cpp
class ISystem {
    /// Cap the number of parallel sub-jobs the engine will dispatch
    /// for this system's `parallelFor` calls. 0 = uncapped (default).
    /// Read once per `update` invocation; pinned during the wave.
    virtual std::uint32_t preferredWorkerCap() const noexcept {
        return 0;
    }
    // ...
};
```

`EngineImpl::parallelFor` reads the cap at submit time and computes
grain such that the resulting sub-job count ≤ cap. The cap is a
ceiling, not a floor — small workloads still pick fewer sub-jobs
based on the normal grain heuristic.

**Public surface change.** Yes — additive virtual on `ISystem`. The
default returning 0 preserves bit-for-bit behavior for every existing
system. Doxygen `@brief` + `@thread_safety` annotations. Coverage
audit entry.

**Empirical finding (post-implementation).** The hypothesized bench
gate — `CubeRenderSystem::preferredWorkerCap()=8` → mean step ≤ 6.5 ms
at workers=71 — did NOT hold. n=10 trials at workers=71 normal mode:

| cap on cube-render | median mean step | range |
|---|---|---|
| 0 (baseline)  | 8.6 ms | 5.8 – 8.8 |
| 32            | 9.4 ms | 7.2 – 10.4 |
| 8             | 9.7 ms | 8.1 – 10.9 |

Cap=8 is a small regression: cube-render's `wait_ms` jumps from
0.03 ms (baseline) to ~2.5 ms because one slow sub-job (8×17.5k rows
each) stalls the wave, instead of 71 workers stealing 284×500-row
sub-jobs in parallel. The 8-worker D12 sweet spot (6.1 ms) was an
**engine-wide** phenomenon — every system pays cv-wakeup overhead
proportional to the pool size, not to a single system's sub-job
count. A per-system fan-out cap can't recover that.

**What T2 actually delivers:** the mechanism is built, tested, and
ready. `CubeRenderSystem` does NOT opt in (returns 0). The
`main.cpp` `cfg.workerCount = stressMode ? 16 : 8` workaround
stays. T3's per-sub-job telemetry + T5's adaptive policy will
revisit `preferredWorkerCap` with data — there may be other systems
where the cap genuinely helps (smaller work per call, where the
straggler isn't a single slow sub-job).

**Test gate.** `tests/worker_cap_test.cpp` lands; asserts (1) cap=0
falls through to the existing grain heuristic (uncapped fan-out =
32 sub-jobs at workerCount=8, count=10000), (2) cap=4 clamps to 4
sub-jobs even when grain=0 would have picked 32, (3) cap overrides
explicit caller-supplied grain that would have produced more
sub-jobs, (4) determinism — cap=0 and cap=2 produce identical
`commitHash` over 5 ticks of a row-writing workload. Full suite
stays 117 → 118/118 green.

**Risk.** Low. Cap is a no-op when 0; existing tests prove the
default path. No opt-in system in the tree changes behavior.

---

### Batch T3 — Telemetry: per-system avg sub-job duration — LANDED 2026-05-23

**Scope.** Extend `SystemStats`:

```cpp
struct SystemStats {
    // ... existing fields ...
    /// EWMA of per-sub-job wall-clock duration in microseconds.
    /// Decay 1/16 to match `avgUpdateSeconds`. Sampled inside
    /// `parallelFor` after each sub-job returns; safe across
    /// workers via per-system atomic accumulator + post-wave
    /// snapshot. 0 if the system never called `parallelFor`.
    double avgSubJobMicros = 0.0;
    /// Sub-jobs submitted this step. Lets the tuner compute
    /// "did we use ≤ cap?" and "should we split more?".
    std::uint32_t subJobsLastStep = 0;
};
```

`EngineImpl::parallelFor` already times the wave; T3 adds per-sub-job
sampling. Adds the values to `writeJsonLines` and `ChromeTraceWriter`
output.

**Public surface change.** Yes — additive fields on `SystemStats`.
`COVERAGE_AUDIT.md` updated.

**Bench gate.** Run `perf_audit_rpg_demo 300` — the per-system rows
now show `subJob_us` and `subJobs`. Cube-render expected ~50–80 µs
per sub-job (the regime where oversubscription bites).

**Empirical result (post-implementation).** `perf_audit_rpg_demo 300`
on the same workers=71 box used for the T2 sweep:

| system      | upd_ms | brf_ms | subJob_us | subJobs |
|-------------|--------|--------|-----------|---------|
| cube-render |  4.945 |  3.446 |      63.5 |   284.0 |

Cube-render lands at **63.5 µs/sub-job × 284 sub-jobs ≈ 18 ms of
parallel work spread across the pool** — matches the planned
50–80 µs envelope. Every other system reports 0 because none call
`parallelFor` (they're single-threaded or sub-job-free), exactly as
designed.

**Test gate.** `tests/sub_job_telemetry_test.cpp`: a busy-spin
workload at 1 ms per sub-job (deterministic, vs sleep_for's HZ
jitter) over 40 ticks. Asserts `avgSubJobMicros ∈ [0.7, 1.3] × truth`
and `subJobsLastStep == 4`. Also covers the quiet-system case
(no parallelFor → both fields stay zero) and EWMA persistence
across a step.

**Risk.** Low. Pure observation. The per-sub-job timing uses a
`std::chrono::steady_clock` pair brackets the user lambda only
(latch bookkeeping is not charged) and folds the ns count into a
single `std::atomic<uint64_t>::fetch_add(relaxed)` per sub-job —
roughly one atomic op per 10–100 µs of useful work. The atomic is
read by the sim thread post-`JobLatch::wait()`, so the relaxed
adds happen-before the read via the latch's count_down/wait
acquire-release pair.

---

### Batch T4 — `ITuningPolicy` + `TuningPatch` plumbing — LANDED 2026-05-23

**Scope.** New public types:

```cpp
struct SystemGrainOverride {
    std::string_view systemName;   // resolved by name lookup
    std::uint32_t    preferredGrain;
};

struct TuningPatch {
    std::vector<SystemGrainOverride> grainOverrides;
    // T5 will add SkipBiasOverride; T6 will add the scripted-replay handle.
};

class ITuningPolicy {
public:
    virtual ~ITuningPolicy() = default;
    virtual void observe(const EngineStats& engine,
                         std::span<const SystemStats> systems,
                         const JobSystemStats& jobs) = 0;
    virtual std::optional<TuningPatch> propose() = 0;
};

/// On `Engine::setTuningPolicy(policy)`:
/// - Engine calls `observe(...)` once per `step()`, AFTER the
///   commit phase, BEFORE the next tick starts.
/// - Engine calls `propose()` once per `step()`. If it returns a
///   patch, the engine applies it at the next tick boundary
///   (before preStep). Mid-wave application is forbidden.
/// - Policy is non-owning; user owns lifetime.
/// - `nullptr` disables tuning (the default).
void Engine::setTuningPolicy(ITuningPolicy* policy);
```

**Public surface change.** Yes — three new types + one `Engine`
method. Header lives at `include/threadmaxx/Tuning.hpp`.

**Bench gate.** `bench/adaptive_tuning_bench` runs the "stable"
workload with a no-op policy (observe but never propose). Mean step
must be within ±2% of the baseline — observe-only path must add
near-zero overhead.

**Bench gate result.** `bench/adaptive_tuning_bench --entities=50000
--ticks=200 --warmup=20`: 3 interleaved rounds (B N B N B N) cancel
ordering bias. Median observe-only overhead **-2.22%** of baseline (noop
faster than baseline is measurement-noise territory, not engine
speedup; positive side stays well under the +2% gate). The
synthetic "stable" workload is 50k entities × Transform-writing
parallelFor with ~250 ns busy-loop per row — mean step ~14 ms,
which is the regime where any observe-only overhead would surface.

**Test gate.** `tests/tuning_patch_application_test.cpp` lands;
asserts (1) `setTuningPolicy(p)` / `tuningPolicy()` round-trip and
`nullptr` clears the staged patch; (2) `observe()` + `propose()`
each fire once per `step()` after stats publish; (3) a patch
proposed on the tick whose end-of-step shows `engine.tick == T`
is APPLIED at the top of the next step — visible to that step's
`parallelFor(grain=0)` dispatch; (4) unknown system names produce
an `ILogger@Warn` line and otherwise no-op; (5) determinism —
two engine instances run with the same scripted policy produce
identical `commitHash` streams over 8 ticks. Full suite stays
119 → 120/120 green.

**Risk.** Medium. New public API surface; needs careful Doxygen +
coverage. The patch-application timing (tick boundary, before
preStep) is load-bearing for determinism — assertion (3) above
pins it.

**What landed.** `include/threadmaxx/Tuning.hpp` ships the three new
types. `Engine::setTuningPolicy(p)` / `tuningPolicy()` plus
`EngineImpl::applyPendingTuningPatch()` (called at the top of
`step()` before `preStep`) carry the staged patch through; the
observe + propose call sites sit AFTER the trace-sink callback in
`step()` so the policy sees the same `EngineStats` /
`SystemStats` / `JobSystemStats` snapshot a sink would see.
`COVERAGE_AUDIT.md` records the new public surface entries. The
v1.3 commit-hash contract is preserved unchanged (tuning is a
scheduling knob; it does not touch storage or commit order).

---

### Batch T5 — Built-in policy: `AdaptiveGrainPolicy` — LANDED 2026-05-23

**Scope.** Header-only built-in policy in
`include/threadmaxx/AdaptiveGrainPolicy.hpp`:

```cpp
class AdaptiveGrainPolicy : public ITuningPolicy {
public:
    struct Config {
        std::uint32_t cooldownTicks       = 60;
        std::uint32_t minSamplesPerChange = 3;
        double        targetSubJobMicros  = 200.0;
        double        explorationEpsilon  = 0.05;
        double        stepSize            = 1.5;   // 50% up/down per change
        std::uint32_t minGrain            = 1;
        std::uint32_t maxGrain            = 65536;
    };
    explicit AdaptiveGrainPolicy(Config c = {});
    void observe(...) override;
    std::optional<TuningPatch> propose() override;
};
```

**Heuristic:**

- For each system with `subJobsLastStep > 0`:
  - Maintain EWMA of `avgSubJobMicros`.
  - If EWMA < `targetSubJobMicros / 2` → coarsen (multiply grain by
    `stepSize`). Sub-jobs were too tiny; dispatch overhead dominated.
  - If EWMA > `targetSubJobMicros * 4` AND `peakQueueDepth ≥ workerCount`
    → split (divide grain by `stepSize`). One worker stalled the wave.
  - Otherwise hold.
- Apply at most one change per system per `cooldownTicks`.
- Require `minSamplesPerChange` consecutive ticks pointing in the
  same direction before acting (anti-noise).
- With probability `explorationEpsilon`, propose a random grain in
  the bounded range (ε-greedy exploration).

**Bench gate.** `bench/adaptive_tuning_bench --mode=tiny-fanout`
sweeps the grain ladder `{64,128,256,512,1024,2048,4096}` to
establish the offline optimum, then runs 200 ticks with
`AdaptiveGrainPolicy` installed. Gate: adaptive mean step ≤ +10% of
offline-optimum mean step. The harness forces `warmup ≥ 100` so the
policy fully converges (first fire tick 3, second tick 63, third
tick 123 …) before the measurement window opens.

**Bench gate result.** `bench/adaptive_tuning_bench --mode=tiny-fanout`
(default 50k entities, ticks=200, warmup=100): offline optimum
`grain=256 mean=12.16 ms`, adaptive `finalGrain=486 mean=12.18 ms`
→ **+0.14%** above optimum. The policy converges to `grain=486` —
not the empirical winner `256`, but well inside the hold band —
yet the mean step difference is below the measurement noise floor
(p95 is essentially identical). The 10% gate has comfortable
headroom for noisier workloads.

**Test gates.** `tests/adaptive_grain_convergence_test.cpp` drives
the policy through a *synthetic* feedback loop (the harness models
`avgSubJobMicros` as `grain × workPerEntityNs / 1000`, no engine in
the loop) so the convergence math is hardware-independent. Asserts:
(1) the policy fires at least once when sub-jobs start below the
hold band's floor; (2) the final simulated sub-job duration lands
inside `[target/2, target*4]`; (3) the policy is quiet for at least
`2 × cooldownTicks` at the end (proves it settled, not oscillating).
Empirical: 7 fires, `finalGrain=1094`, `finalMic=109.40 µs`, silent
tail of 237 ticks.

`tests/adaptive_no_oscillation_test.cpp` exercises two invariants
against the same synthetic harness: (i) pinning sub-job duration at
10 µs every tick produces fires at least `cooldownTicks` apart AND
every cooldown-sized sliding window contains at most one fire
(empirical: 7 fires across 400 ticks, gaps ∈ {60, 60, 60, 60, 60, 60});
(ii) alternating between deep-coarsen and deep-split signals
produces zero fires — the streak counter resets on direction flips
so the streak gate never opens.

**Risk.** Medium. The heuristic is the core IP. Watch for: (1)
oscillation on noisy workloads — hysteresis + the cooldown gate
covers this; (2) divergence when the workload itself is changing —
bounded step size + cooldown caps the damage. The wide hold band
(`[target/2, target*4]`) is a deliberate precision-for-stability
trade: the policy converges into a band, not onto a single point.
The +0.14% bench result shows the band is tight enough in practice.

**What landed.** `include/threadmaxx/AdaptiveGrainPolicy.hpp`
ships the header-only policy + Config + inspection helpers
(`lastAppliedGrain` / `lastChangeTick`). `bench/adaptive_tuning_bench`
gains `--mode=tiny-fanout` (sweep + adaptive comparison) alongside
the existing `--mode=stable` (T4 no-op overhead gate). Two new
tests register with CTest. CLAUDE.md grows an `AdaptiveGrainPolicy`
paragraph in the Adaptive-tuning section; `COVERAGE_AUDIT.md`
records the new public surface entries.

---

### Batch T6 — Determinism mode + scripted replay — LANDED 2026-05-23

**Scope.** Three modes:

```cpp
enum class TuningMode {
    Off,      // setTuningPolicy(nullptr); default.
    Active,   // setTuningPolicy(policy); policy.propose() is live.
    Scripted, // setTuningPolicy(policy); policy.propose() ignored —
              //   patches come from a recorded `TuningTrace` instead.
};

class TuningTrace {
public:
    void record(std::uint64_t tick, const TuningPatch& p);
    bool tryGet(std::uint64_t tick, TuningPatch& out) const;
    void serialize(std::ostream&) const;
    static TuningTrace deserialize(std::istream&);
};
```

The networked / replay flow:

1. Authoritative server runs in `Active` mode. Every applied patch
   is recorded into `TuningTrace`.
2. Server broadcasts the trace alongside the input log.
3. Clients run in `Scripted` mode against the trace.
4. Server + every client produce bit-identical `commitHash` streams
   tick-for-tick.

This mirrors §3.11.5's `SkipPolicy::Scripted` precedent: same shape,
same testability.

**Public surface change.** Added `TuningMode` enum + `TuningTrace`
class in `include/threadmaxx/Tuning.hpp`. Engine plumbing:
`Engine::setTuningMode` / `tuningMode()`, `Engine::setTuningTrace` /
`tuningTrace()`. Backwards-compat: `setTuningPolicy(p)` implicitly
selects `TuningMode::Active` when `p != nullptr` and `Off` otherwise,
so every pre-T6 caller keeps working without touching mode at all.

`TuningTrace` is a sorted vector of `(tick, TuningPatch)` entries
with O(log n) `tryGet` and a wire format of
`[magic 'TUNE' u32][version u32][entryCount u64]` followed by
`[tick u64][overrideCount u64]{[nameLen u64][name][grain u32]}*`.
The `record` / `tryGet` API matches the spec; `serialize` /
`deserialize` mirrors `WorldSnapshot`'s host-endian POD style.

**Bench gate.** `bench/adaptive_tuning_bench --mode=scripted` —
records a TuningTrace from an Active AdaptiveGrainPolicy run on
the tiny-fanout workload, then replays it in Scripted mode (no
policy installed). Asserts:
- `commitHash[tick]` is bit-identical for every measured tick.
- Scripted mean step time is within +2% of Active.

Landed: 200 measured ticks, identical hash through end-of-run
(last = `0x1b799c7e15f70a86`); scripted runs **−7.44%** vs Active
(observe/propose overhead skipped, well inside the +2% gate).

**Test gate.** `tests/adaptive_determinism_test.cpp`:
- TuningTrace round-trip (record → serialize → deserialize → tryGet).
- Garbage / truncated input rejected (deserialize returns empty).
- Active mode records exactly the patches the policy emitted,
  keyed by the proposing tick.
- 10-tick Active run + 10-tick Scripted replay produces identical
  `commitHash` streams.
- Scripted mode with no trace attached is inert (engine still ticks).

**Risk.** Low. The trace format is small (`AdaptiveGrainPolicy`
emits at most one entry per `cooldownTicks` per system; tiny-fanout
landed 5 entries over 200 measured ticks). Serialization is byte-
identical across runs because the trace is built deterministically
from the same input stream.

---

## 5. What this plan deliberately does not do (yet)

- **No skip-pressure tuning in T1–T6.** ChatGPT's design note mentions
  raising grain on tiny tasks under frame-budget pressure and
  lowering priority for non-essential systems. That's a sensible T7
  but not in scope here — `SkipPolicy::Budget` already covers the
  worst-case ("we're over budget; skip the marked systems"). Adding
  finer pressure modulation needs more data.
- **No auto-workerCount.** Locked behind §2's principle. T1's
  per-call cap + T2's per-system cap give us the per-system
  fan-out control without touching the pool size.
- **No general "optimize everything" policy.** Each system that
  wants tuning opts in via `preferredWorkerCap()` or by being
  named in a `TuningPatch`. Tuner does not crawl the system list
  and rewrite knobs unprompted.
- **No tuning of game-side knobs (e.g. NPC counts).** Tuning is for
  engine-internal scheduling; gameplay is the game's job.

## 6. Bench-gate quick reference

| Batch | Bench | Gate |
|---|---|---|
| T1 | `perf_audit_rpg_demo 300 --workers=71` normal | mean step ≤ 8 ms |
| T2 | `tests/worker_cap_test` (engine mechanism gate) | sub-job count clamped to cap; commitHash unchanged |
| T3 | `perf_audit_rpg_demo 300` + `tests/sub_job_telemetry_test` | cube-render `subJob_us ∈ [50, 80]` (landed: 63.5); EWMA converges within ±30% of truth on a busy-spin |
| T4 | `tests/tuning_patch_application_test` + `bench/adaptive_tuning_bench` (stable workload, no-op policy, 3 interleaved rounds × 200 ticks) | mechanism: observe/propose pumped per step + patch applied next-tick + scripted determinism holds; bench: median noop overhead ≤ +2% of baseline (landed: -2.22%, comfortably within noise) |
| T5 | `bench/adaptive_tuning_bench --mode=tiny-fanout` + `tests/adaptive_grain_convergence_test` + `tests/adaptive_no_oscillation_test` | bench: adaptive mean step ≤ +10% of offline grain-sweep optimum (landed: +0.14%); tests: convergence into hold band + ≤ 1 fire per cooldown window + 0 fires under alternating direction signals |
| T6 | `bench/adaptive_tuning_bench --mode=scripted` + `tests/adaptive_determinism_test` | bench: identical commitHash for all 200 measured ticks; scripted-path overhead ≤ +2% (landed: −7.44%); tests: TuningTrace round-trip + Active→Scripted replay hash equality |

## 7. Definition of done for Phase T

- All six batches landed; each commit references its `T#` in the
  message.
- `tests/adaptive_tuning/` is green on `build/` (Release) and
  `build-werror/` (strict) trees.
- `examples/rpg_demo/main.cpp` no longer hard-codes `cfg.workerCount`
  — instead, `CubeRenderSystem::preferredWorkerCap()` returns 8 and
  the engine picks the rest.
- `doc/performance_tuning.md` gains an "Adaptive tuning" section
  covering `setTuningPolicy`, `AdaptiveGrainPolicy`, and the
  Scripted-mode replay flow.
- `CLAUDE.md` gains the new public surface entries (Tuning.hpp +
  AdaptiveGrainPolicy.hpp + ISystem::preferredWorkerCap +
  SystemStats fields).
- `CHANGELOG.md` updated; library version bumped to v1.4 (the public
  API additions clear the v1.3 floor; commit-hash contract from B30
  is unchanged).

## 8. Connection to the audit data

The starting point for everything above is the D12 audit's worker
sweep table in §1. The plan is sized to take us from "we hard-coded
the right answer for one workload" to "the engine self-tunes for
arbitrary workloads with bounded risk." If T1–T5 don't materially
improve `perf_audit_rpg_demo`'s 71-worker number, the heuristic is
wrong and we revisit the design before continuing.

Determinism (T6) is the long-term guarantee that makes this
acceptable for networked games — same input + same scripted patch
stream = same world. Without T6 the tuner is a desktop-only
convenience; with T6 it survives in production.
