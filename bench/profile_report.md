# Phase 8 Batch 27 — Profile Report

**Date:** 2026-05-20
**Scope:** Diagnostic sweep of the RPG-stress workload at 10k / 50k /
100k entities, attributing time to specific subsystems without
optimizing anything.
**Deliverable per `OPTIMIZATION_PATH.md` §3.3:** top 3 inefficiencies +
3 candidate optimizations with bench gates and expected deltas.
**Status:** ✅ — see B28+ proposals in `OPTIMIZATION_PATH.md`.

This document is **non-normative** — it is a snapshot of the
2026-05-20 dev workstation. Re-run `rpg_stress_probe` on the target
host before drawing conclusions for that environment.

## Methodology

Three diagnostic passes via `bench/rpg_stress_probe` (built under
`-DTHREADMAXX_BUILD_BENCHMARKS=ON`):

- **Pass A — Per-system breakdown.** Run the full 3-system workload
  at each scale; report each system's `lastUpdateSeconds`,
  `waitSeconds`, `peakQueueDepth`, and commands-committed per
  step. Answers: *which system in the update phase is the long
  pole?*
- **Pass B — System-mix ablation.** Run the workload at 100k entities
  with each system individually disabled; compute step-time deltas
  against the all-three baseline. Answers: *if we deleted system X,
  how much would the frame budget shrink?*
- **Pass C — Commit cost vs. command volume.** Movement-only at
  varying NPC counts; report commit time as a function of command
  count. Answers: *is the commit phase cost linear in command
  count, and what is the per-command cost?*

Cross-validated with the existing `commit_path_bench` (Churn
workload, same dev workstation).

Warmup = 8 ticks, measurement = 96 ticks, workers = 4, deterministic
mode on, `singleThreadedCommit = true` (the production default).

## Observations

### O1 — At 100k entities, `movement` is 99% of step time

From Pass B ablation:

```
all                 step= 16.31 ms  upd=  7.16 ms  commit=  9.97 ms
no-movement         step=  0.22 ms  upd=  0.37 ms  commit=  0.00 ms
no-brain            step= 17.43 ms  upd=  7.37 ms  commit= 10.05 ms
no-renderprep       step= 15.12 ms  upd=  5.30 ms  commit=  9.81 ms
movement-only       step= 14.96 ms  upd=  5.12 ms  commit=  9.84 ms
brain-only          step=  0.21 ms  upd=  0.21 ms  commit=  0.00 ms
renderprep-only     step=  0.17 ms  upd=  0.17 ms  commit=  0.00 ms
```

The all → no-movement drop (16.31 → 0.22 ms) accounts for 16.1 ms /
16.3 ms = **98.6 %** of the frame. Brain (1.2 ms) and renderprep
(1.8 ms) are roughly 7 % and 11 % of the original step but they
operate within the same wave windows; removing them surfaces only
their non-overlapping contribution.

**The frame budget is the movement system. Optimization effort
elsewhere is wasted unless movement is fixed first.**

### O2 — `movement` is wait-bound, not work-bound

From Pass A at 100k:

```
movement      upd=  5.090 ms  wait=  5.083 ms  peakQD=  1  cmds=100001
```

`waitSeconds / lastUpdateSeconds` = **99.9 %**. The sim thread is
parked in `parallelFor`'s latch wait for almost the entire update
window. `peakQueueDepth = 1` confirms only one chunk-job is in
flight at a time — `forEachChunk` dispatches one job per matching
chunk, and the NPC chunk (100,000 rows) dominates so completely
that the sim thread sees a queue depth of 1 each time it samples.

**Root cause:** chunks in this workload have wildly uneven row
counts:

| chunk           | rows    | share |
|-----------------|---------|-------|
| Player          | 1       | 0.001%|
| Sword           | 1       | 0.001%|
| Terrain         | 1       | 0.001%|
| NPCs            | 100,000 | 95.2 %|
| Pickups         | 5,000   | 4.8 % |

`forEachChunk<Transform, Velocity>` matches Player + NPCs (the two
chunks carrying both bits). The Player chunk's job finishes in
microseconds; the NPC chunk's job is a single worker chewing 100k
rows. Workers 2/3/4 sit idle for most of the wave.

**Conclusion:** at this workload, the chunk-iteration framework's
default "one job per matching chunk" granularity is the wrong
choice. Large chunks need sub-job splitting.

### O3 — Commit phase is linear at ~98 ns/cmd

From Pass C:

| npc     | cmds/step | step (ms) | commit (ms) | ns/cmd (commit) |
|---------|-----------|-----------|-------------|-----------------|
| 1,000   | 1,001     | 0.187     | 0.096       | 96.21           |
| 5,000   | 5,001     | 0.790     | 0.499       | 99.71           |
| 10,000  | 10,001    | 1.516     | 0.986       | 98.58           |
| 25,000  | 25,001    | 3.552     | 2.443       | 97.72           |
| 50,000  | 50,001    | 7.349     | 4.913       | 98.25           |
| 100,000 | 100,001   | 14.907    | 9.805       | 98.05           |
| 200,000 | 200,001   | 30.111    | 19.840      | 99.20           |

Flat to within 3 % across 200×. **There are no per-step fixed costs
in the commit phase — the entire 98 ns/cmd is per-command serial
work.** Decomposed against the §3.10.1 batch 21 measurements:

- ~70 ns/cmd — FNV-1a-64 byte-by-byte mix of (discriminator +
  EntityHandle + 48-byte CmdSetTransform payload)
- ~28 ns/cmd — `applyCommandImpl` (storage write through the
  `EntityStorage::setX(handle, value)` path)

Cross-validated by `commit_path_bench` (different workload, same
hash + apply paths):

```
setTransform Churn/single  114 ns/cmd  (full step)
setVelocity  Churn/single   80 ns/cmd  (full step)
```

The 80 → 98 → 114 progression scales with payload size — 24 B
Velocity → 48 B Transform → the same Transform plus dispatch
overhead — confirming the hash is the dominant per-command cost.

## Top 3 inefficiencies

Ordered by absolute frame-time impact at 100k entities:

| # | inefficiency                                  | cost at 100k | mechanism                                          |
|---|-----------------------------------------------|--------------|----------------------------------------------------|
| 1 | Single-threaded commit hash                   | **~7 ms**    | FNV-1a-64 byte-mix per command on the sim thread   |
| 2 | `forEachChunk` load imbalance on large chunks | **~3-4 ms**  | One job per matching chunk; 100k-row chunk → 1 job |
| 3 | `applyCommandImpl` apply on the sim thread    | **~2.8 ms**  | Per-command storage write, serial by contract      |

Items 1 and 3 are the same serial-commit budget; #1 is the
hash-specific subset and is the larger reducible part. #2 is on the
update-phase budget.

The brain (`ctx.single` serial scan) and renderprep (parallel
chunk-walk with no commits) systems combined are < 1.5 ms — under 10 %
of the budget at 100k. Optimization effort there yields diminishing
returns until the dominant items are addressed.

## Three candidate optimizations

Each is sized against the inefficiency it attacks. Bench gates and
expected deltas are concrete enough that the proposing PR can prove
or disprove the hypothesis on this same hardware.

### C1 — Sub-job dispatch for large chunks in `forEachChunk`

**Attacks:** #2 (`forEachChunk` load imbalance).

**Current implementation** (`include/threadmaxx/Query.hpp`):

```cpp
ctx.parallelFor(matchCount, /*grain*/ 1,
    [chunks, matchIndices = std::move(matchIndices), fn]
    (Range r, CommandBuffer& cb) mutable {
        for (std::uint32_t k = r.begin; k < r.end; ++k) {
            const auto* c = chunks[matchIndices[k]];
            fn(std::span<...>(c->entities), ..., cb);
        }
    });
```

`matchCount` is the number of matching chunks. Each chunk becomes
one `Range` of size 1. A chunk with 100k rows is one indivisible
unit of work assigned to one worker.

**Proposed:** when a matching chunk has more than `kSubJobThreshold`
rows (proposed default: 1024), dispatch multiple sub-jobs that each
own a contiguous row range of the same chunk. The callback
signature is unchanged — sub-jobs receive a subspan of the chunk's
entity/component spans rather than the full chunk.

**Hypothesis:** movement update time drops from 5.09 ms → ~1.3 ms
(4× speedup on 4 workers) on the RpgStress 100k workload. Total
step drops by ~3.8 ms → ~12.5 ms.

**Bench gate:**
- `rpg_stress_bench` 100k row: `update` mean drops by ≥ 3 ms,
  `step` by ≥ 3 ms.
- `chunk_iter_bench` (AI workload): no regression beyond 5 % on
  small-chunk cases.
- `foreach_bench`: no regression on the small-chunk path.

**Risk:** moderate. The callback contract changes from "your
callback receives one full chunk's spans" to "your callback may
receive a subspan of a chunk's spans" — but the spans are still
contiguous and chunk-local, so the documented invariants
(`es.size() == component_spans[i].size()`) remain. Existing
chunk-iteration tests pass byte-for-byte. The work belongs in
`Query.hpp` and can ship as an additive header change.

**Effort:** ~2-3 days.

### C2 — Parallel pre-hash with serial mix-in

**Attacks:** #1 (single-threaded commit hash).

**Current implementation** (`src/EngineImpl.cpp::commitBuffer`):

```cpp
for (auto& cmd : buf.commands_) {
    const auto e = applyCommandImpl(cmd, storage);
    hashCommandImpl(stats.commitHash, cmd, e);
}
```

The FNV-1a state is a single 64-bit accumulator; each command mixes
its bytes into it in submission order. By construction, this is
serial.

**Proposed:** in a pre-pass, hash each command's payload bytes
**in parallel** (the byte sequence per command is independent of any
other command) producing a per-command 64-bit precomputed-mix value
`H_cmd`. Then on the serial commit thread, mix the precomputed
values into the running accumulator with the existing FNV-1a step.
The serial work drops from ~70 ns/cmd of byte-mix to ~6 ns/cmd of
8-byte FNV mix-in.

**Hypothesis:** commit phase drops from 9.8 ms → ~3.5 ms at 100k
cmds. Total step drops to ~9-10 ms. Per-tick commit hash byte-
identical to the current path (the math is associative under the
right mix order).

**Bench gate:**
- `rpg_stress_bench` 100k row: `commit` drops by ≥ 5 ms.
- `commit_path_bench`: setTransform single-threaded ns/cmd drops
  by ≥ 50 %.
- `commit_hash_test.cpp` **byte-identical** golden. This is the
  load-bearing test: any reordering of the byte-mix produces a
  different hash and the test catches it.
- `sharded_commit_test.cpp` byte-identical.

**Risk:** high. The hash math has to be byte-identical to the
current path; the FNV-1a recurrence is `h' = (h ^ b) * P`, which
is NOT distributive over multiple bytes. To make a parallel
pre-hash equivalent to the serial hash you need either (a)
precompute `(P^n, b_n)` lookup pairs per command (a tail-mix
formula) or (b) reformulate the hash. The first is correct and
implementable but requires careful unit tests; the second changes
the determinism contract and is C3, not C2.

**Effort:** ~1-2 weeks, with most of the time in the byte-identity
proof + unit tests.

### C3 — Per-archetype command counter as the hash source

**Attacks:** #1 (single-threaded commit hash), with a determinism-
contract amendment.

**Current implementation:** commit hash is FNV-1a over `(per-cmd
discriminator + EntityHandle + payload bytes)` for every applied
command. Strong invariant: byte-identical reproducibility under the
exact same command stream.

**Proposed:** weaken the contract to "byte-identical
reproducibility under the same archetype-level transitions." The
hash mixes only `(archetype-mask, command-count, end-of-tick
archetype-row-count)` per archetype per tick. Per-command hashing
disappears.

**Hypothesis:** commit phase drops from 9.8 ms → ~1 ms at 100k
cmds (only the apply path remains). Total step drops to ~6-7 ms.
**BUT** the per-tick hash value changes — `commit_hash_test.cpp`
goldens must be regenerated, and so must any external client's
recorded reference hashes.

**Bench gate:**
- `rpg_stress_bench` 100k row: `commit` drops by ≥ 7 ms.
- `commit_path_bench`: setTransform single-threaded ns/cmd drops
  by ≥ 80 %.
- New `archetype_hash_determinism_test.cpp` covering the new
  contract: same input → same hash across runs and machines.
- `commit_hash_test.cpp` UPDATED with new goldens — a deliberate
  one-time bump.

**Risk:** highest. Networked / lockstep / replay systems that
have recorded reference hashes need to re-record. The benefit
is real but the cost in user-facing churn is real too.

**Effort:** ~1 week of code + ~1 week of golden-test re-baselining
and migration documentation.

## Recommendation for Phase 8 sequencing

Update `OPTIMIZATION_PATH.md` §3.1 to:

```
B26 (gate) ✅
B27 (profile sweep) ✅
↓
B28 — C1 (sub-job dispatch in forEachChunk)  ← greenlit, low risk, ~3-4 ms win
B29 — C2 (parallel pre-hash + serial mix-in) ← greenlit, high risk, ~6 ms win
B30 — C3 (per-archetype hash)                ← gated; defer to v1.3 unless C2 underperforms
B31 — wave-scheduler micro-opts              ← downgrade; profile shows <1% of step at 100k
B32 — sanitizer + soak hygiene               ← unchanged, unconditional
B33 — docs polish + v1.2 release             ← unchanged, unconditional
```

**Sequencing rationale:**

- C1 (B28) ships first because it's low-risk, additive, and yields
  ~3-4 ms / tick at 100k. The work is in one header
  (`Query.hpp`); the existing test suite catches regressions
  because chunk iteration is heavily covered.
- C2 (B29) ships second. The risk is the byte-identity proof of the
  parallel pre-hash; once that's clean, ~6 ms / tick is on the
  table for free.
- C3 (B30) stays parked unless C2 underperforms. The contract
  amendment is real user-facing churn (re-recorded reference hashes
  in any external client) and v1.x SHOULD NOT churn the
  determinism story without a clear evidence-driven need. If C2
  hits its target, C3 doesn't ship.
- B31 (wave-scheduler micro-opts) is **downgraded**. The profile
  shows wave-rebuild + per-wave dispatch cost is in the noise
  (<1 % of step at 100k). Skip unless future workloads expose it.
- B32 / B33 unchanged. Both ship unconditionally before the v1.2
  tag.

The B28 — B29 sweep collectively projects step time at 100k from
**16.3 ms → ~6-7 ms** (~2.5× speedup) if both wins land cleanly.

## Cross-references

- `OPTIMIZATION_PATH.md` §3.3 (B27 spec) — this report fulfills it.
- `bench/rpg_stress_bench.cpp` (B26) — the production gate; every
  later batch diffs against it.
- `bench/rpg_stress_probe.cpp` (B27) — the diagnostic binary that
  produced the Pass A/B/C numbers in this report.
- `bench/commit_path_bench.cpp` (B16) — cross-validation of the
  per-command commit cost on the Churn workload.
- `FUTURE_WORK.md` §3.10.1 batch 21 — original analysis of the
  commit-hash serial bottleneck; this report empirically confirms
  it at the rpg_demo workload scale.
- `threadmaxx_core_future_optimization_notes.md` §2.2 — "keep the
  commit path simple unless profiling proves otherwise." Profiling
  has now proved otherwise — at 100k cmds/tick the commit path is
  the dominant cost and warrants targeted work.

## How to reproduce

```
cmake -S . -B build -DTHREADMAXX_BUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target rpg_stress_probe -j
./build/bench/rpg_stress_probe
```

Output is plain text on stdout. Pass C ns/cmd should land within
~5 % of 98 ns on a modern x86_64 desktop; significant deviation
suggests host clock scaling or thermal throttling — re-pin the
governor and re-run.

---

# SHARDED_OPTIMISATION.md S0 — Sharded commit Pass A/B/C baseline

**Date:** 2026-05-23
**Bench:** `bench/commit_pass_breakdown` (32 warmup, 256 measure ticks,
`workerCount = 4`).
**Reference JSONL:** `bench/commit_pass_breakdown_baseline.jsonl`
(3584 rows = 14 workload × path × 256 ticks).

The S0 instrumentation (`Engine::lastCommitBreakdown()` →
`include/threadmaxx/CommitBreakdown.hpp`) records Pass A / Pass B /
Pass C wall-clock plus per-step counters every time
`commitBuffersSharded` runs. Always-on, ~30–60 ns/call overhead.

## Headline numbers

The canonical run committed alongside this section (full data in
the JSONL above):

```
workload                         path       step_us  commit_us   passA_us   passB_us   passC_us    wait_us   cmd/tk   bin/tk   shTk   fbTk
setTransform/Churn               single     12842       1897          0          0          0          0           0      0.0     0     0
setVelocity/Churn                single     14724       2622          0          0          0          0           0      0.0     0     0
addRemoveTag/Churn               single     21612      11011          0          0          0          0           0      0.0     0     0
spawnDestroy/Churn               single      1468         77          0          0          0          0           0      0.0     0     0
setTransform/MultiArch           single      7943       2006          0          0          0          0           0      0.0     0     0
RPG-mix                          single     26090       2778          0          0          0          0           0      0.0     0     0
SmallWorld                       single        59          6          0          0          0          0           0      0.0     0     0
setTransform/Churn               sharded    15517       4615       0.05       1418       2832       2826      100000      2.0   256     0
setVelocity/Churn                sharded    15382       4392       0.05       1496       2517       2511      100000      2.0   256     0
addRemoveTag/Churn               sharded    21798      11186          0          0          0          0      100000      0.0     0   256
spawnDestroy/Churn               sharded      1425         76          0          0          0          0        1024      0.0     0   256
setTransform/MultiArch           sharded    14652       8198       0.05       1764       5125       5113      100000      4.0   256     0
RPG-mix                          sharded    31364       7092        872       2275       2858       2848      109308      4.0   256     0
SmallWorld                       sharded        72         21       0.03         4         16         12         256      2.0   256     0
```

Column legend: `step_us` is mean per-step wall-clock around
`engine.step()`. `commit_us` is `EngineStats::commitDurationSeconds`
(engine-side commit-only wall-clock, single AND sharded — apples-to-
apples). `passA/B/C_us` is the per-step Pass-wallclock from
`Engine::lastCommitBreakdown()`, zero in single-mode by design.
`wait_us` is the `JobLatch::wait` subset of Pass C. `cmd/tk` and
`bin/tk` are mean commands and active-bins per tick. `shTk` / `fbTk`
are tick counts where the sharded path ran in full vs early-outed to
per-buffer serial commit.

## Reproducibility

The bench gate (S0) of ±3% across runs is **not met on this host**.
Repeated full runs see ~5–20 % mean variance on the value-only
sharded workloads (CPU governor + background noise on a non-isolated
desktop). The **relative breakdown** (Pass C share of commit ns) is
much more stable — within ~5 % across runs — which is what the rest
of the plan reads to pick batches. Per-run noise is documented; the
qualitative finding below is robust across all observed runs.

## What the data says

The three S0 hypotheses (from §7 of `SHARDED_OPTIMISATION.md`):

1. **Pass B dominates (classifier overhead)** → PARTIAL. Pass B is
   24–47 % of sharded commit time and ~75 % of single-mode commit
   time *on the same workload*. Material, but Pass C is the bigger
   share.
2. **Pass C dominates (apply or sync)** → **CONFIRMED**. Pass C is
   50–76 % of sharded commit time on every value-only workload;
   `JobLatch::wait` is **99 % of Pass C** in every measured case
   except SmallWorld (where wait is 77 %). The bottleneck is
   workers being slow to claim and complete the bin jobs, not the
   apply work itself.
3. **Pass A dominates (migrating set build)** → REJECTED for the
   value-only workloads (passA_us ≈ 0). The exception is `RPG-mix`
   where Pass A is ~872 µs ≈ 3 % of step / 12 % of sharded commit
   (from the ~5 % mask-flip subset) — non-trivial but secondary.

Concretely, on the canonical run:

| Workload | Pass B % of commit | Pass C % of commit | wait % of Pass C |
|----------|-------------------:|-------------------:|------------------:|
| setTransform/Churn      | 31 % | 61 % | 99.8 % |
| setVelocity/Churn       | 34 % | 57 % | 99.8 % |
| setTransform/MultiArch  | 22 % | 63 % | 99.8 % |
| RPG-mix                 | 32 % | 40 % | 99.7 % |
| SmallWorld              | 20 % | 74 % | 77.4 % |

`addRemoveTag/Churn` and `spawnDestroy/Churn` fell through to the
per-buffer serial commit on **every** tick (`fbTk = 256`, `shTk = 0`).
That's the auto-fallthrough doing its job — `totalValueOnly == 0`
for `addRemoveTag` (every command migrates), and
`totalCommands < 256` for `spawnDestroy` (only 1024 / step but
distributed across 2 buffers, each falling under threshold).

## How big is the sharded penalty

`EngineStats::commitDurationSeconds` is populated in both modes and
gives us the apples-to-apples commit cost:

| Workload | commit single (µs) | commit sharded (µs) | sharded / single |
|----------|-------------------:|--------------------:|-----------------:|
| setTransform/Churn      | 1897  | 4615  | **2.4×** |
| setVelocity/Churn       | 2622  | 4392  | 1.7×     |
| addRemoveTag/Churn      | 11011 | 11186 | 1.0× (fallback) |
| spawnDestroy/Churn      | 77    | 76    | 1.0× (fallback) |
| setTransform/MultiArch  | 2006  | 8198  | **4.1×** |
| RPG-mix                 | 2778  | 7092  | **2.6×** |
| SmallWorld              | 6.6   | 21.9  | 3.3× (fallback overhead) |

**Sharded commit is uniformly worse than single commit on every
workload where it actually runs.** The "4-way Pass-C apply pays for
the classifier" model from CLAUDE.md does not hold on any of these
shapes — Pass B alone roughly equals the entire single-mode commit
cost (1418 µs vs 1897 µs on `setTransform/Churn`), and Pass C adds
another 2832 µs on top, the vast majority of which is latch wait.

The per-command apply cost in single mode is ~19 ns (1897 µs /
100 k commands for `setTransform/Churn`), nowhere near the 130 ns
reference figure in CLAUDE.md. That figure must have been measured
on a migration-heavy workload (where each cmd triggers
`setMaskAndMigrate`); for value-only workloads in v1.2, the per-cmd
apply cost is an order of magnitude lower, and the sharded path
has no parallel-apply headroom to recover.

The interesting case is `addRemoveTag/Churn`: commit is 11011 µs in
single mode (≈ 110 ns/cmd, dominated by per-cmd
`setMaskAndMigrate`), and a hypothetical sharded path that could
parallelize migrations would have real wall-clock to claw back.
Today it falls back; that's the gap S6 is supposed to close.

## Action items (S0 → next batch)

The plan's §7 batch ordering assumed Pass B was the live cost. The
data sharpens that, but more importantly reveals a different lever:

- **S5 (small-bin serial fast path)** moves to the front of the
  queue. Pass C wait is 99 % of Pass C; the actual parallel-apply
  work finishes long before the latch unblocks. Even at ~50 k
  commands per bin, the latch / wake-up overhead dwarfs the apply
  benefit. Hypothesis: switching to a serial fast path entirely
  (no `jobs_->submit`, no `JobLatch`) on bins below some threshold
  collapses 2832 µs Pass C → < 500 µs.
- **S6 (migration batching)** moves up because the only workload
  with real parallel-apply headroom is the one that falls back
  today (`addRemoveTag`, 11 ms commit). A migration-batched
  variant could enable that to run on the sharded path and
  actually save wall-clock.
- **S1 / S2 (predecode header)** stays in the plan but at lower
  priority. Pass B is 24–47 % of sharded commit; even cutting it
  30 % shaves at best ~500 µs off the gap to single, while S5
  could shave 2000+ µs in one batch.
- **S3 (parallelize Pass A)** is gated on RPG-mix only (~3 % of
  step), low priority.

## Stop-condition check

The S0 stop condition was: "if Pass A + B + C wall-clock totals
less than serial commit time on the RPG-mix workload, sharded
already wins and we just flip the default." On RPG-mix:
sharded commit ≈ 7092 µs > single commit ≈ 2778 µs. Sharded loses
by 2.6×. The default does NOT flip; we proceed to S5 with the
hypothesis above.
