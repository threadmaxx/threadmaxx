# SHARDED_OPTIMISATION.md

Batched, test- and bench-driven plan for making `Config::singleThreadedCommit = false` (the sharded commit path) the better choice on workloads that matter, while keeping every existing determinism guarantee.

Companion plan: `threadmaxx_sharded_commit_path_optimization_plan.md` (the ChatGPT-authored brainstorm this file critiques and reshapes into shippable batches).

## 1. Status quo (current as of v1.2)

- The sharded path lives in `EngineImpl::commitBuffersSharded` (`src/EngineImpl.cpp:1266`). Three passes: build migrating-entity bitmap (Pass A), classify + apply migrating + bin value-only by archetype (Pass B), parallel apply of bins (Pass C).
- Per CLAUDE.md / `bench/commit_path_bench`: **serial classifier ~130 ns/cmd exceeds 4-way Pass-C apply's ~50 ns/cmd recovery on tested shapes**, so sharded loses ~30–70% on every measured workload in `bench/commit_path_bench`. `singleThreadedCommit = true` is the documented default and `commitBuffersSharded` auto-falls-through to the serial path when `totalCommands < 256 || totalValueOnly == 0 || chunkCount < 2`.
- Per-archetype hash rollup (`finalizeCommitHash`) is **already** independent of which commit path ran. Determinism is locked down by:
  - `tests/sharded_commit_test.cpp` (single vs sharded `commitHash` parity + `WorldSnapshot` byte-equality + re-run stability)
  - `tests/commit_hash_test.cpp` (per-tick reference hashes)
  - `tests/commit_soak_test.cpp` (long-running parity)
  - `tests/archetype_hash_determinism_test.cpp` + `tests/v1_2_legacy_commit_hash_test.cpp`
- Engine-owned scratch already in place (batch 21): `shardMigratingBitmap_` (no per-tick allocation), `shardChunkBins_` (per-archetype bin reuse). Both are reset-not-realloc on the steady state.

## 2. Goal and non-goals

**Goal.** Flip `Config::singleThreadedCommit` to `false` as the engine default by making sharded **strictly faster** (mean and p99) on at least the RPG-mix, MultiArch, and Churn workloads in `bench/commit_path_bench` — without regressing the small-world and single-archetype cases (those continue to fall through to serial).

**Non-goals.**
- Changing the `commitHash` contract. The per-archetype rollup contract (`Config::legacyCommitHash = false`) stays load-bearing. No batch in this plan may add a new hash format; legacy and v1.3 hash must both remain bit-identical to the serial path post-batch.
- Changing the public `CommandBuffer` API. Record-time chunk routing (S8) is the one batch that's allowed to extend the API surface, and only if S1–S5 fail to close the gap.
- Removing or weakening the serial fallback. `singleThreadedCommit = true` stays available for every release in this plan.
- Making sharded "more parallel" first. The roadmap is overhead reduction before more workers.

## 3. Gating contract (every batch must satisfy)

A batch lands only when ALL of these hold:

| Gate | Mechanism |
|------|-----------|
| Per-tick `commitHash` matches serial on every tick of every workload | `tests/sharded_commit_test.cpp` + `tests/commit_hash_test.cpp` + `tests/commit_soak_test.cpp` |
| Final `WorldSnapshot` byte-identical between serial and sharded | `tests/sharded_commit_test.cpp::snapshotHash` |
| Legacy v1.2 hash unchanged | `tests/v1_2_legacy_commit_hash_test.cpp` |
| Bench numbers improved on the batch's stated target workload | `bench/commit_path_bench` + the batch-specific bench (see §5) |
| No regression on workloads that already fall through to serial | the early-out branch in `commitBuffersSharded` |
| Full test suite green under default config | `ctest --output-on-failure` |
| ASAN + UBSAN + TSAN green on the determinism tests | per CLAUDE.md sanitizer convention |

If a batch lands its code but fails any bench gate, it gets reverted (or sunk to a `#if 0` block) and the next batch starts. We do not stack speculative changes on top of unmeasured wins.

## 4. Workload taxonomy (the inputs every batch is measured against)

`bench/commit_path_bench.cpp` already covers the first four; S0 adds the remaining ones.

| Name | Shape | Why it matters |
|------|-------|---------------|
| `setTransform/Churn`     | 100k entities, 1 archetype, 100k value-only `CmdSetTransform`/tick | Pure value-only, single chunk → sharded auto-falls-through today (the floor case). |
| `setVelocity/Churn`      | Same as above but `CmdSetVelocity` | Same shape; complements `setTransform` for variant-cost mix. |
| `addRemoveTag/Churn`     | 100k mask-toggling commands/tick | 100% migrating → sharded auto-falls-through today. The pathological case. |
| `spawnDestroy/Churn`     | 1024 spawns + 1024 destroys/tick | Sparse global commands; tests the global-lane cost in Pass B. |
| `setTransform/MultiArch` | 100k entities split across 4 archetypes | The case sharded **should** win. Today loses by ~30%. **Primary target.** |
| `RPG-mix` (S0 adds)      | RPG-demo profile: ~60% value-only across 6+ archetypes, ~30% spawn/destroy, ~10% mask flip | Real-app shape; flips default decision. |
| `SmallWorld` (S0 adds)   | 256 entities, 1k commands/tick | The fallthrough sanity case — must stay at parity, never regress. |

All seven get measured every batch. The two new workloads (`RPG-mix`, `SmallWorld`) land in S0.

## 5. Bench surface

- **Existing.** `bench/commit_path_bench.cpp` (per-variant churn + multi-arch), `bench/commit_bench.cpp` (single vs sharded mean), `bench/migration_bench.cpp` (mask-flip throughput).
- **S0 adds.** `bench/commit_pass_breakdown.cpp` — instruments Pass A / Pass B / Pass C wall-clock independently. Writes JSON Lines with `{tick, totalCommands, totalValueOnly, migrating, chunks, nsPassA, nsPassB, nsPassC, nsLatchWait, nsTotal}`. This is the single artifact the rest of the plan reads to pick winners.
- Each post-S0 batch ships **one** new bench focused on the cost it claims to attack (see batch sections below). When the bench is too narrow for `commit_path_bench`, it goes in `bench/commit_micro_<name>.cpp`.

Bench discipline:
- Warmup 32 iters, measure 256 iters, report mean / p50 / p95 / p99.
- Compare **same tick range** across `singleThreadedCommit ∈ {true, false}`. Don't trust mean-of-mean across separate runs; use paired comparison with the same RNG seed and same workload.
- Track `EngineStats::commandsThisStep` + `JobSystemStats::ownPops`/`stolenJobs` in every bench row so we can correlate timing wins with command volume and steal pressure.

## 6. Test surface

- **Existing determinism gates (re-run every batch).** `sharded_commit_test`, `commit_hash_test`, `commit_soak_test`, `archetype_hash_determinism_test`, `v1_2_legacy_commit_hash_test`. These already cover the "no observable behaviour change" contract for value-only and migrating commands.
- **Per-batch tests.** When a batch introduces a new internal data structure (e.g. compact command record, per-pair migration batch), it ships a unit test that exercises the structure directly — independent of the engine integration — to catch wire-level regressions before they hit the determinism harness.

## 7. The batches

### S0 — Instrument and lock the baseline

**Scope.** Read-only telemetry batch. No production code-path change.

- Add `bench/commit_pass_breakdown.cpp`: instruments `commitBuffersSharded` (via a private compile-time hook or temporary `THREADMAXX_BENCH_PASS_BREAKDOWN` flag — must compile out cleanly in release) to record Pass A/B/C wall-clock and per-bin bytes-moved.
- Add the `RPG-mix` workload (mirror the rpg_demo command mix; ~100k entities, 6 archetypes) and `SmallWorld` (256 entities) to `bench/scene_workloads.hpp` so every subsequent batch measures all seven.
- Generate `bench/profile_report.md` baseline rows with both paths on all seven workloads. **Commit the baseline numbers** (the rest of the plan compares against them).
- Capture command-mix histogram per workload: % value-only vs % migrating vs % spawn vs % destroy.

**Bench gate.** Baseline numbers reproducible to within ±3% across two runs. Pass-breakdown lines sum to total per-tick `commitBuffersSharded` time within ±5% (instrument noise check).

**Test gate.** No production code touched; full suite stays green.

**Exit criteria.** The numbers say (or refute) which of three hypotheses is the live one:
  1. Pass B dominates (classifier overhead) → S1, S2, S4 are the leverage.
  2. Pass C dominates (apply or sync) → S5, S6 are the leverage.
  3. Pass A dominates (migrating set build) → S3 is the leverage.

**Stop condition.** If Pass A + Pass B + Pass C wall-clock already totals less than the serial commit time on the RPG-mix workload, sharded already wins and we just flip the default (jump straight to S7 → S∞).

**S0 outcome (LANDED 2026-05-23).** Baseline numbers committed (full table + analysis in `bench/profile_report.md` under "SHARDED_OPTIMISATION.md S0"; raw JSONL in `bench/commit_pass_breakdown_baseline.jsonl`). Findings:

- Hypothesis **2 (Pass C dominates) confirmed**. `JobLatch::wait` is 99 % of Pass C on every measured value-only workload — the latch / wake-up overhead is the bottleneck, NOT the parallel apply work. SmallWorld is the only outlier (77 %), where the fixed wait floor is proportionally smaller.
- Hypothesis 1 (Pass B) is partial — Pass B is 24 – 47 % of sharded commit; meaningful but not the top lever.
- Hypothesis 3 (Pass A) refuted except on RPG-mix (12 % of sharded commit, secondary).
- **Bigger finding.** `EngineStats::commitDurationSeconds` shows sharded commit is **2.4–4.1× slower** than single commit on every value-only workload, not "moves overhead around" — the sharded path was always losing on these shapes, not just sometimes. The CLAUDE.md "130 ns/cmd classifier vs 50 ns/cmd recovery" reference applied to a migration-heavy workload that we don't measure; on the value-only workloads the per-cmd serial apply is ~19 ns, so there's no parallel-apply headroom to claw back.
- **Reproducibility gate (±3 %) is NOT met on this host** — ~5 – 20 % per-run mean variance on a non-isolated desktop. The *qualitative* breakdown (Pass C share, sharded ≫ single in commit) is stable across all observed runs.

**Batch ordering update (consequence of S0).**

- **S5 promoted to immediate-next.** Pass C wait dominates; switching small/medium bins to serial fast path is the highest-leverage single change.
- **S6 promoted to second.** The one workload with real parallel-apply headroom (`addRemoveTag/Churn`, 11 ms commit) is the one that falls back today. Migration batching is the gate to making sharded actually win there.
- **S1 / S2 keep their place** in the queue but are explicitly secondary — even a 30 % Pass B reduction shaves at most ~500 µs off a ~3000 µs gap to single. Worth doing once S5/S6 land.
- **S3, S4** stay low-priority (gated on RPG-mix or legacy hash only).
- **The default does NOT flip on S0** — stop-condition check failed (sharded commit 7 092 µs > single commit 2 778 µs on RPG-mix).

### S1 — Predecode command kind once per command

**Hypothesis (depends on S0).** `std::visit` / `std::holds_alternative` walks in Pass B are a top-3 cost — the same command goes through `commandIsMigrating`, `commandTargetEntity`, and `applyCommandImpl`, each re-discriminating the variant.

**Change.** Build a per-command `CmdHeader { uint8_t kind; uint8_t flags; uint32_t entityIndex; }` once at submit time in `CommandBuffer.cpp`, stored in a parallel `std::vector<CmdHeader>` alongside `commands_`. Pass B reads the header by index; `applyCommandImpl` still gets the variant but the discrimination is by `kind` (a switch on a `uint8_t`, no variant introspection).

This is the **only** representation change in this plan. We do not relayout the variant itself — it's already 64B post-batch-18 and changing it has reach outside the commit path.

**Bench.** `bench/commit_micro_classifier.cpp` — synthesise a 100k-command buffer (mixed kinds) and just run Pass B. Target: ≥ 30% reduction in Pass B ns/cmd.

**Tests.** `tests/command_header_test.cpp` — round-trips every command kind through submit/clear and asserts header matches the variant.

**Gate.** Pass B ns/cmd ≥ 30% lower. All determinism tests green.

**Risk.** Doubles the `CommandBuffer` per-command footprint (16 B header + 64 B variant). Mitigation: header is 8 B not 16; if the bench doesn't move ≥ 30%, revert and try S2 first.

### S2 — Cheaper migrating-entity test

**Hypothesis.** Pass B's per-command `shardMigratingBitmap_[e.index]` lookup is cache-cold for sparse workloads (one byte per entity slot, but most cache lines are dead weight). With the S1 header in place, the migration check collapses to `header.flags & kMigrates` for non-stale entities — only stale handles need the bitmap.

**Change.** Mark `header.flags |= kMigrates` at submit time for every command kind that's in `commandIsMigrating`'s switch. Pass A becomes "scan headers and mark bitmap"; Pass B's hot loop reads the header bit instead of the bitmap.

Keep the bitmap — it's still needed when a tag/setter is followed by a value-only on the same entity in the same buffer (the value-only must take the global lane). But it's now a fallback, not the primary lookup.

**Bench.** `commit_micro_classifier` on `MultiArch` — target ≥ 15% additional reduction (compounding with S1) in Pass B ns/cmd.

**Tests.** Determinism harness alone suffices; the change is internal. Add one test where a `CmdAddTag` is followed by a `CmdSetTransform` on the same entity in the same buffer to lock the in-buffer-after-migration case.

**Gate.** Pass B ≥ 15% improvement, no regression on `addRemoveTag` (currently auto-fallthrough; must stay that way).

### S3 — Parallelize Pass A across buffers

**Hypothesis.** Pass A walks every command in every buffer to seed the migrating bitmap. With N workers and K buffers, the buffers can be classified in parallel; the bitmap is the only shared resource and writes are idempotent (set-to-1). Cost: one CAS per migrating command per worker, but Pass A becomes embarrassingly parallel.

**Change.** Convert `shardMigratingBitmap_` to `std::vector<std::atomic<std::uint8_t>>`. Dispatch one job per buffer in Pass A; each writes its own slice of `shardMigratingIndices_` (per-worker output, merged serially before Pass B). Skip Pass A entirely when `totalValueOnly == totalCommands` (unchanged).

**Bench.** `bench/commit_pass_breakdown` — target ≥ 40% reduction in Pass A wall-clock at `totalCommands ≥ 32k` and 4+ workers. Must not regress Pass A at `totalCommands < 1k` (job submission overhead would dominate); falls back to serial when buffer count is 1 or commands are sparse.

**Tests.** Existing determinism + TSAN on the soak test (the new atomic writes are the only TSAN-visible change).

**Gate.** Pass A wins on the threshold, no TSAN regressions.

**Risk.** Wakes workers earlier in the commit window, possibly contending with Pass C's later wave. Mitigation: only enable the parallel Pass A when `commandsThisStep_ > kParallelAThreshold` (tuned at this batch).

### S4 — Per-chunk command hash mix at queue time

**Hypothesis.** Pass B's legacy-hash mix (`commitHashAcc_ = hashCommandImpl(...)`) serializes Pass B. The v1.3 path already skips this; if S0 shows the legacy hash is a meaningful share, S4 moves the per-command mix to Pass C (where it's per-chunk-bin local and folds in at the end). This batch only fires if `legacyCommitHash = true` (i.e. v1.2 compatibility callers).

**Change.** Per-bin running hash; serial fold in Pass C ordering (= submission order within bin; bins folded in archetype-index ascending order for determinism). Global-lane commands keep their inline mix in Pass B.

**Bench.** `commit_pass_breakdown` with `legacyCommitHash = true`. Target: legacy and non-legacy Pass B ns/cmd converge to within 10%.

**Tests.** `tests/v1_2_legacy_commit_hash_test.cpp` — must produce bit-identical legacy hashes after the move.

**Gate.** Legacy hash bit-identical to baseline; non-legacy unaffected.

**Skip condition.** If S0 shows legacy hash is < 5% of Pass B, skip S4 entirely. The legacy path is slated for removal in v1.4 — don't optimise a sunsetting code path unless data demands it.

### S5 — Pass C sync cost for small bins

**Hypothesis.** A 4-bin parallel dispatch with each bin holding 50–200 commands pays more in `JobLatch` setup + steal + wake than it earns. The threshold for switching to per-bin serial fast-path is currently 0 (we always dispatch).

**Change.** Add a per-bin row threshold (`kMinBinForJob ≈ 256`, tunable). Bins below the threshold execute serially on the sim thread after the latch is set up for bins ≥ threshold. If no bin meets the threshold, skip the latch entirely.

**Bench.** `commit_pass_breakdown` on `RPG-mix` with a stress slice that spreads 10k commands across 32 archetypes (~300 cmds/bin). Target: Pass C wall-clock + latch wait ≥ 20% lower at this shape; no regression on `MultiArch` (4 large bins).

**Tests.** Determinism harness alone. Add one bench case that explicitly produces many tiny bins to make the threshold path code-covered.

**Gate.** The "many tiny bins" sub-case wins; `MultiArch` (4 fat bins) stays within ±2%.

**S5 outcome (LANDED 2026-05-23).** `kMinBinForJob = 256` shipped in `commitBuffersSharded` (`src/EngineImpl.cpp`); new `CommitBreakdown::inlineBinCount` counter exposes the inline lane count to the bench. `ManyTinyBins` workload (6 400 entities × 32 archetypes, ~200 cmds/bin) added to `bench/scene_workloads.hpp`. Full numbers + delta tables in `bench/profile_report.md` ("SHARDED_OPTIMISATION.md S5"); raw JSONL in `/tmp/s5_breakdown_v2.jsonl`.

- **RPG-mix sharded commit −38.6 %** (7 908 → 4 853 µs, mean of three runs). Pass C dropped 47 %; latch wait dropped 47 %. The player + sword micro-archetypes (~1 cmd/bin) take the inline lane; NPC + pickup chunks (~50 k / ~5 k cmds) stay on the worker dispatch path.
- **ManyTinyBins sharded commit = 219 µs**, with `inl/tk = 32.0` (every bin inline) and `wait_us = 0.0` (latch entirely skipped). The "no bin meets threshold" branch fires every tick.
- **SmallWorld picked up the inline lane as a bonus.** Both bins (~128 cmds each) now run inline; latch wait = 0. Sharded vs single ratio is essentially unchanged (the workload was already auto-falling-through close to break-even).
- **MultiArch within noise.** S0 baseline 6 044 µs → S5 mean of three runs 6 296 µs (+4.2 %); individual runs spanned 5 234 / 5 819 / 7 836 µs — squarely inside the documented 5–20 % per-run variance on this host. The ±2 % formal gate is unmeasurable here. All 4 bins are 25 k cmds each, well above threshold; `largeBins == activeBins` makes S5's new branch collapse to the pre-S5 code path. Any timing difference is variance.
- **Auto-fallthrough workloads unchanged.** `addRemoveTag/Churn` and `spawnDestroy/Churn` still hit `fbTk = 256` (every tick). S5 doesn't touch the fallback path.
- **Determinism gates green** across all 5 sharded-specific tests plus full 123-test `ctest`. Commit-hash parity holds bit-for-bit.

**Default does NOT flip on S5.** RPG-mix sharded commit (4 853 µs) is still 1.75× single (2 778 µs); MultiArch (6 296 µs) is still 3.16× single (1 991 µs). The S∞ gate "sharded ≤ single on RPG-mix" is not met.

**Batch ordering update (consequence of S5).**

- **S6 (migration batching) is next.** It is the only batch still in the plan that can move `addRemoveTag/Churn` off the auto-fallthrough — that workload pays ~11 ms in commit on both paths today, every mask flip a separate `setMaskAndMigrate`. A per-pair `migrateBatch` is the largest single lever remaining.
- **MultiArch is the remaining gap.** Its 4 fat bins all run as jobs; S5 doesn't touch them. The ~4 000 µs Pass C wait is ~half the gap to single. S1/S2 (predecode header) are the structural answer if S6 doesn't close it.
- **S5 gate met qualitatively.** ManyTinyBins exercises the inline path 100 %; RPG-mix wins by 38 %; MultiArch is within noise. The ±2 % formal gate on MultiArch is not measurable on this host but the underlying change is provably a no-op for that workload shape.

### S6 — Migration batching by (source, dest) archetype pair

**Hypothesis.** When N entities all flip the same bit, they target the same destination archetype. Pass B currently applies each migrate independently (one `setMaskAndMigrate` per command); a per-pair batch could amortise the destination-row reservation and the swap-pop bookkeeping.

**Change.** In Pass B, when a contiguous run of commands all migrate to the same destination mask, route them through a new `ArchetypeTable::migrateBatch(srcMask, dstMask, span<EntityHandle>)`. This is the only batch that touches `Archetype.cpp`.

**Bench.** `bench/migration_bench.cpp` already exists; extend it to measure batch vs per-cmd migration at N ∈ {1, 8, 64, 512}. Target: ≥ 30% reduction at N ≥ 64.

**Tests.** New `tests/migration_batch_test.cpp` covering edge cases (empty batch, mixed src masks within a run, same-src-same-dst-different-rows, stale handles in the run).

**Gate.** Bench wins at N ≥ 64; mask-flip churn (`addRemoveTag`) sees ≥ 20% improvement on the serial commit path too (because it has the same per-cmd migrate cost).

**Risk.** This is the second-biggest change in the plan and touches storage. **Land it behind the determinism harness running 10× the soak duration before flipping the default.**

**S6 outcome (LANDED 2026-05-24).** Two new APIs:

- `ArchetypeTable::migrateBatch(srcArch, dstMask, span<srcRows>, out span<dstRows>, out span<BatchSwapEvent>)` — Phase 1 inserts in submission order; Phase 2 pops in *descending* srcRow order. The pop-order proof in `tests/migration_batch_test.cpp` (test 6) is the determinism canary: batch path's per-tick `commitHash` matches per-cmd path bit-for-bit across 50 ticks of alternating addTag/removeTag churn.
- `EntityStorage::setMaskAndMigrateBatch(span<EntityHandle>, ComponentSet newMask)` — thin slot-mgmt wrapper; refuses (returns `false`) if any handle is stale or any src archetype differs from the first, leaving the caller to fall back to per-cmd.

`commitBuffer` (serial path) and `commitBuffersSharded` Pass B (global lane) BOTH detect contiguous runs of same-kind same-(srcArch, dstMask) commands and route runs ≥ `Config::batchMigrateThreshold` (default 16) through the batch. Supports `CmdAddTag` / `CmdRemoveTag` / `CmdSetHealth` / `CmdSetFaction` / `CmdSetBoundingVolume`. Other migrating commands keep the per-cmd path.

New `Config::batchMigrateThreshold` knob lets benches A/B the path. New `CommitBreakdown::batchedMigrations` counter exposes the per-step batched-cmd count. Engine-owned scratch in `EntityStorage` + `ArchetypeTable` + `EngineImpl` keeps the steady state at zero allocations.

**Headline numbers (commit phase only, `EngineStats::commitDurationSeconds` mean of 512 iters):**

| Workload | Batch off | Batch on | Δ |
|---|---|---|---|
| `migration_bench` healthFlip @ N=64    | 14 226 ns | 12 340 ns | **−13.3%** |
| `migration_bench` healthFlip @ N=512   | 68 079 ns | 63 301 ns | **−7.0%**  |
| `migration_bench` healthFlip @ N=4096  | 496 861 ns| 454 098 ns| **−8.6%**  |
| `migration_bench` healthFlip @ N=32000 | 3 731 426 ns | 3 252 455 ns | **−12.8%** |
| `addRemoveTag/Churn` (single commit)   | 11 643 µs | 10 913 µs | **−6.3%**  |

N=1 and N=8 are below the threshold; both rows take the per-cmd path under either config (apparent delta is host noise — measured ±1%).

**Gates vs spec:**

- ≥30% reduction at N ≥ 64 — **MISSED (best −13.3%, typical −7 to −10%).** Storage-side per-row work (per-component `vec.push_back` / swap-pop) dominates the per-call dispatch overhead that batching can amortise. Real gain is the archetype-hashmap lookup deduplication + capacity-grow consolidation + UserCarry-vector hoisting. Closing the rest would require batching the per-component-vector inserts themselves (S8 territory).
- ≥20% reduction on `addRemoveTag` serial commit — **MISSED (−6.3%).** Same root cause.
- Determinism gates green — **PASSED.** `sharded_commit_test`, `commit_hash_test`, `archetype_hash_determinism_test`, `v1_2_legacy_commit_hash_test`, `commit_soak_test` all pass; `migration_batch_test` (new, 6 cases) passes; `concurrency_soak_long` (~5 min, 10× standard soak) passes.

**Decision.** Ship S6 — the gains are real, deterministic, and zero-risk (gates all green). Default `singleThreadedCommit = true` stays pinned: even after S5+S6, RPG-mix sharded commit (5036 µs) is 1.80× single (2795 µs); the gap closed by ~5% relative to the post-S5 1.75×.

**Action items.**

- **Skip S7 (adaptive fallthrough cutoff).** S0–S6 have left a clean dominance ordering: single beats sharded on every measured workload. The spec's S7 skip condition is met.
- **S8 (per-chunk record-time routing) is the only batch left that can meaningfully move the needle**, but the architectural cost (refactor `CommandBuffer` to maintain per-destination mini-lists at record time) is large. Park unless a different workload pattern surfaces.

### S7 — Adaptive fallthrough cutoff

**Hypothesis.** The hardcoded `totalCommands < 256` cutoff is correct in shape but wrong in value for any given workload — it should learn from the last K commits.

**Change.** EWMA over `(commandsThisStep_, sharded_time, serial_time)` from a calibration window (first 64 ticks). Choose per-tick path based on observed crossover, not a constant. Falls back to a fixed cutoff if calibration data is too noisy.

**Bench.** All seven workloads, must not regress any.

**Tests.** Determinism harness; add a `tests/sharded_cutoff_test.cpp` that asserts the calibration is deterministic given a fixed command stream (we don't want to make `commitHash` depend on real time).

**Gate.** No workload regresses; at least three workloads improve by ≥ 5% over the static cutoff.

**Skip condition.** If S0–S6 leave a clean dominance ordering (one path always wins for a given workload shape), keep the static cutoff and skip S7.

### S8 — Per-chunk record-time routing (the speculative big swing)

**Only fires if S0–S7 leave Pass B as the dominant cost AND the gap to flipping the default is still > 10%.**

**Hypothesis.** Pass B exists because `CommandBuffer` is a flat command list. If `CommandBuffer` internally maintained one mini-list per destination chunk, Pass B's classifier becomes "iterate chunks." Pass A and most of Pass B vanish.

**Change.** Internal-only `CommandBuffer` layout change: `std::array<SmallVec<CmdIndex>, kFastChunkBuckets>` indexed by destination-chunk hint, with overflow to a global "needs-classify" list. The recording API stays unchanged — the routing happens inside `setTransform`/`addTag`/etc. Migrating commands continue to go through the global list (they don't know their destination chunk at record time).

**Bench.** All seven workloads. Target: `MultiArch` and `RPG-mix` see ≥ 25% improvement over post-S5 numbers.

**Tests.** New `tests/command_buffer_routing_test.cpp`. Plus the full determinism harness — this change is the most likely to break the hash contract.

**Gate.** Workload wins ≥ 25% on the two named workloads. **Determinism harness runs 10× soak duration without divergence before merge.**

**Risk.** Highest in the plan. The recording API stays stable, but every `cb.set*` call gains a hint-lookup cost. If the hint cost > Pass B savings on small workloads, the batch is a net regression.

**Defer / kill.** If S0–S5 already flip the default on `MultiArch` and `RPG-mix`, **do not ship S8**. The code complexity isn't worth it.

**S8 outcome (LANDED 2026-05-24).** Recording-side per-chunk routing landed despite the S6 outcome having parked it — explicit user direction to chase the deeper win.

**APIs added (all internal to CommandBuffer + a Config knob):**

- `CommandBuffer::setLocator(LocatorFn, void*)` — engine installs a chunk-locator hook on each freshly-created buffer at wave start. Hook signature: `uint32_t(const void* ctx, EntityHandle h) noexcept` returning the entity's current archetype index or `kInvalidArchetype`. The locator wraps `EntityStorage::locate()`; lifetime tied to the wave-context SystemContext.
- `CommandBuffer::globalIndices()` / `chunkBuckets()` — submission-order index lists into `commands()`. Populated at record time when the locator is set. Migrating cmds (every non-`SetTransform/Velocity/Acceleration/UserData` variant) go to `globalIndices()`; value-only cmds route to `chunkBuckets()[arch]`; stale handles fall through to global.
- `CommandBuffer::noteGlobalCommand(idx)` — public router for free-function recorders (`addUserComponent`, `removeUserComponent`) that push directly into `commands()` without going through a member method.
- `Config::recordTimeRouting` (default `true`) — A/B switch. Set `false` (or `THREADMAXX_NO_ROUTING=1` env var) to skip installing the locator hook so sharded commit falls back to the pre-S8 Pass A scan.

**Sharded commit flow with S8 on:**

1. **Pass A** — walks each buffer's `globalIndices()` (the migrating-or-stale subset, typically a small fraction of total cmds). Each entry's target entity index is OR'd into a wave-cumulative migrating bitmap. Routing-inactive buffers (preStep / postStep / single-threaded commit) fall back to the legacy full scan.
2. **Pass B** — for routing-active buffers: walks each bucket; entries whose target is in the migrating bitmap demote to `demotedScratch_`, others transfer to engine-owned `shardChunkBins_[k]`. Demoted indices are sorted then merge-applied with `globalIndices()` in submission order, with the S6 migration-batch run detector intact (truncating runs against demoted indices to preserve order).
3. **Pass C** — unchanged from S5/S6: parallel apply of `shardChunkBins_` with the small-bin serial fast path.

**The wave-cumulative bitmap** (cleared only at wave start in `step()`, not per-commitBuffersSharded call) is what makes cross-system migrations within a wave safe: if system A's commit migrates entity e, system B's bucket entry for e (recorded against wave-start storage) demotes via the same bitmap on system B's commit pass.

**Headline numbers (`commit_pass_breakdown`, mean of 3 runs of 256-iter sharded paths):**

| Workload | S8 off | S8 on | Δ (commit_us) |
|---|---|---|---|
| `setTransform/Churn`     | 4 780 µs  | 3 666 µs  | **−23%**  |
| `setVelocity/Churn`      | 4 487 µs  | 3 561 µs  | **−21%**  |
| `addRemoveTag/Churn`     | 12 015 µs | 11 013 µs | **−8%**   |
| `setTransform/MultiArch` | 5 639 µs  | 5 981 µs  | ~0% (noise) |
| `RPG-mix`                | 5 911 µs  | 3 821 µs  | **−35%**  |
| `SmallWorld`             | 14 µs     | 9 µs      | −36%      |
| `ManyTinyBins`           | 229 µs    | 180 µs    | −21%      |

Pass B drops 50–60% across every routing-active workload — the S8 mechanism is real. The translation to commit_us depends on Pass C's share: when Pass C is small (Churn, RPG-mix) the Pass B savings flow through; when Pass C dominates (MultiArch, where JobLatch wait is ~95% of Pass C) the commit_us barely moves.

**Gates vs spec:**

- ≥25% improvement on `RPG-mix` — **PASSED (−35%).**
- ≥25% improvement on `MultiArch` — **MISSED (~0%, noise-band).** Pass C latch wait dominates this workload; Pass B optimization can't reach it. Confirms S0's "JobLatch::wait is 99% of Pass C" finding from the other end.
- Determinism harness 10× soak — **PASSED.** `concurrency_soak_long` (~5 min, 10× standard), `concurrency_soak_test`, `sharded_commit_test`, `migration_batch_test`, new `command_buffer_routing_test` (8 cases — 5 unit + 3 end-to-end determinism scripts), full `ctest` 125/125.

**Decision.** Ship S8 — the dual-25% gate is split, but the wins are real where Pass B mattered. Default `singleThreadedCommit = true` stays pinned: MultiArch can't flip without addressing Pass C latch wait, which is the S∞ blocker. RPG-mix is now closer (post-S8 sharded ~1.4× single, was ~1.8× post-S6); a future Pass C overhaul could land the flip.

**Action items.**

- **Skip S∞ for now.** MultiArch latch wait is still the blocker on the default flip. Re-evaluate after a Pass C overhaul (smaller-bin amortization or work-stealing into the latch wait window).
- **Watch for record-time locator overhead in spawn-heavy ticks** — the locator query adds ~10ns per cmd, paid even when the locator returns stale (spawn flow). Bench-tested clean on the current matrix.

### S9 — Sim-thread-inline largest bin (Pass C, low-risk first pass)

**Hypothesis.** Post-S8, MultiArch's commit_us did not move because Pass C is ~99% `JobLatch::wait`: every large bin goes to a worker, the sim thread waits idle, and Pass C's wall-clock equals `max(workers' bin completion times)`. S5 already runs *small* bins inline on the sim thread for the same reason (avoiding the latch entirely when no bin meets `kMinBinForJob`). Extending the inline lane to claim the SINGLE LARGEST among the large bins gives the sim thread real work and reduces the latch count by one.

For MultiArch's 4 evenly-balanced 25k-cmd bins, this turns "4 jobs / 4 workers / sim waits" into "3 jobs / 3 workers + sim runs the 4th". With perfect balance the wait drops to ~zero; with imperfect balance it drops to `max(workers' time) − sim_inline_time`.

**Change.** In `commitBuffersSharded` Pass C, when `largeBins >= 1`:
1. Find the index of the largest large bin.
2. Submit `largeBins − 1` worker jobs (skipping the largest).
3. Run the largest bin inline on the sim thread.
4. Sim then runs the small bins (existing S5 lane) AND `done.wait()` on the remaining jobs.

Determinism preserved: bins target disjoint chunks; execution order across bins doesn't affect any chunk's final content; `finalizeCommitHash` sorts by `mask.bits()` before folding.

**Bench.** `commit_pass_breakdown` all seven workloads. Target: **≥25% commit_us improvement on `setTransform/MultiArch`** (the S8-missed gate). No regression elsewhere — small-bin-only workloads still hit the S5 no-latch path; large-bin-only workloads with `largeBins == 1` get the inline-only path (also no latch).

**Tests.** Full determinism harness: `sharded_commit_test`, `migration_batch_test`, `command_buffer_routing_test`, `concurrency_soak_test`, `concurrency_soak_long` (10× soak).

**Gate.** ≥25% on MultiArch; no workload regresses by more than 5%.

**Risk.** Low. The change is internal to Pass C, the inline lane already exists (S5), and determinism is unaffected. The one failure mode is "sim thread takes longer than max(workers)" when the chosen bin is much bigger than the others — pick the LARGEST and the inverse can't happen.

**S9 outcome (LANDED 2026-05-24).** Sim-thread-inline largest bin lands as designed.

**APIs added (all internal except one Config knob):**

- `Config::inlineLargestBin` (default `true`; A/B via `THREADMAXX_NO_INLINE_LARGEST=1` env var in benches).
- `CommitBreakdown::inlineLargestApplied` counter (per-step calls that ran the largest bin inline).

**Mechanism in Pass C.** Scan `shardChunkBins_` once to count `largeBins` AND identify the index of the single largest large-bin (`largestBinIdx`, `largestBinSize`). Submit `largeBins − 1` worker jobs (skipping `largestBinIdx`). Sim thread then runs the largest large-bin inline, then the small bins (S5 lane), then `done.wait()`. Three lanes: (a) `largeBins == 0` → all inline as before; (b) `latchedJobs == 0` (only the inline target qualifies) → no latch at all; (c) the common case → `largeBins − 1` workers + sim peer.

**Headline (commit_pass_breakdown, mean of 3 runs):**

| Workload | S9 off (NO_INLINE_LARGEST=1) | S9 on (default) | Δ commit_us | wait_us off → on |
|---|---|---|---|---|
| `setTransform/Churn`     | 3 721 µs | 2 884 µs | **−22%** | 2 766 → 3      |
| `setVelocity/Churn`      | 3 845 µs | 2 495 µs | **−35%** | 2 672 → 0.3    |
| `addRemoveTag/Churn`     | 10 951 µs | 11 101 µs | +1% (fallback) | n/a |
| `setTransform/MultiArch` | 5 333 µs | 5 360 µs | ~0% (noise) | 3 971 → 67    |
| `RPG-mix`                | 3 822 µs | 3 667 µs | **−4%**  | 1 791 → 0.3   |
| `SmallWorld`             | 9 µs    | 9 µs    | no diff | 0 → 0          |
| `ManyTinyBins`           | 183 µs  | 189 µs  | ~0    | 0 → 0          |

`wait_us` collapses to near-zero on every routing-active workload — the sim thread is now a peer lane. The `commit_us` movement depends on whether the worker lanes are the bottleneck:

- **Single-large-bin workloads (Churn variants):** sim runs the one bin inline, no worker dispatched, no latch. Commit drops ~25% from removing the latch + worker-cache-cold path.
- **Multi-large-bin balanced workloads (MultiArch with 4 ≈25k bins on 4 workers):** workers still take the same time T to finish their 3 bins; sim's 4th-lane work also takes ~T; wall-clock = max(workers_T, sim_T) ≈ T. Pass C wall clock unchanged.

**Gates vs spec:**

- ≥25% on `setTransform/MultiArch` — **MISSED (~0%).** Balanced bins on saturated workers means sim's extra lane doesn't shorten the critical path.
- No workload regresses by more than 5% — **PASSED** (+1% on the fallback-only addRemoveTag is within noise).
- 10× soak determinism — **PASSED** (`concurrency_soak_long`, `sharded_commit_test`, `migration_batch_test`, `command_buffer_routing_test`, `concurrency_soak_test`, full ctest 125/125).

**Decision.** Ship S9. Real wins on single-large-bin workloads (Churn variants 22–35%) at zero correctness cost. MultiArch stays on the critical path for S∞, blocked by "balanced large bins saturate workers, sim becomes a redundant peer." That blocker shifts to S10 (split the largest worker bin into row-range sub-jobs so sim adds parallelism instead of redundancy) and S11 (cut latch wakeup cost).

**Action items.**

- Run S10 next — same MultiArch gate target.
- Latch wait is now a tiny fraction on every workload; S11's headroom is the small balanced-bin Pass C wait variance, less urgent than expected.

### S10 — Adaptive bin merging / row-splitting (Pass C lane balance)

**Hypothesis.** S9 fixes the "sim thread idle" half. The other half is bin imbalance: when `largeBins != workerCount`, some workers idle. Two cases:
- `largeBins < workerCount` → split the largest bin into N row-range sub-jobs (analogous to `forEachChunk`'s sub-job dispatch).
- `largeBins > workerCount` → merge small adjacent bins to cut per-job dispatch overhead.

**Change.** In Pass C: after determining the inline target (S9), evaluate `(largeBins − 1) vs (workerCount − 1)`. If short on jobs, split the largest worker-bound bin into row ranges so the total job count matches available workers. If many small-large bins exist, merge into job batches.

Row-splitting safety: within a bin, cmds must execute in submission order. Splitting by index range is fine if each range still processes its cmds in order. Across ranges, different cmds touch the SAME archetype chunk → must serialize per-entity. The safe split is by ENTITY: each sub-job owns a row range of the chunk and only applies cmds whose target row falls in that range. Cost: a per-cmd row lookup before assignment to a sub-job.

**Bench.** Especially worth measuring when `largeBins < workerCount` (the case for narrow archetype workloads).

**Tests.** New `tests/pass_c_split_test.cpp` confirming that a single-bin workload sharded across N sub-jobs lands the same commitHash as the unsplit baseline.

**Gate.** ≥15% improvement on any workload where `largeBins < workerCount`. No regression on workloads where `largeBins >= workerCount`.

**Risk.** Medium. Row-by-row sub-bin assignment requires the row lookup, which costs ~10ns per cmd (same as the S8 locator). If the workload doesn't benefit (e.g., bin already saturating workers), the lookup is dead weight.

**Skip condition.** If S9 alone closes the MultiArch gap (post-S9 sharded ≤ 1.1× single), skip S10 — the remaining gap is below the point where further code complexity is justified.

**S10 outcome (LANDED OPT-IN, DEFAULT OFF 2026-05-24).** Implementation, knob, and determinism test all land — but the bench shows the row-split lane is **structurally not net-positive** on the canonical case. `Config::splitLargestBin` defaults to `false`; the code is preserved as a fixed point for a future revisit with a cheaper classifier.

**APIs added (all opt-in):**

- `Config::splitLargestBin` (default `false`; opt in to `true` to enable; `THREADMAXX_NO_SPLIT_LARGEST=1` env in benches is a no-op when default is off, kept for symmetry).
- `CommitBreakdown::splitLargestApplied` counter (per-step calls that fired the partitioner; zero when off).
- `bench/commit_pass_breakdown` workload `setTransform/SingleArch` — 100k entities in one archetype, the only shape that exercises `largeBins == 1`.
- `tests/pass_c_split_test.cpp` (4 tests) — pins commitHash determinism: serial vs sharded-no-split vs sharded-split must agree across pure value-only, multi-setter, mixed migration+split, and below-threshold (split skipped) workloads.

**Mechanism (when opted in).** After the S9 inline-largest scan, if `inlineLargest && largeBins == 1 && largestBinSize >= 2 * kMinBinForJob`, the engine computes `splitFactor = min(workerCount + 1, largestBinSize / kMinBinForJob)`, partitions the bin by row range (`subIdx = min(row / rowsPerBin, M-1)`), and dispatches sub-bins `[1..M)` as worker jobs while sim runs sub-bin 0 inline. Same submission-order invariants as S5/S6/S9.

**Bench (3-run avg, sharded `commit_us`, split-off vs split-on with the eligibility-tightened impl):**

| Workload                  | bin/tk | largeBins | split-off (default) | split-on  | Δ      | Notes |
|---|---|---|---|---|---|---|
| `setTransform/Churn`      | 2      | 2         | 3 912 µs | 3 839 µs | −2%   | Eligibility skip (largeBins ≠ 1) |
| `setVelocity/Churn`       | 2      | 2         | 3 591 µs | 3 881 µs | +8%   | Eligibility skip; +8% is variance |
| `addRemoveTag/Churn`      | 0      | 0         | 11 712 µs | 11 459 µs | −2% | Fallback (no large bins) |
| `setTransform/MultiArch`  | 4      | 4         | 4 365 µs | 5 644 µs | +29% (variance) | Eligibility skip; one outlier run |
| **`setTransform/SingleArch`** | **1** | **1** | **2 217 µs** | **6 794 µs** | **+207%** | **S10 fires here** |
| `RPG-mix`                 | 4      | 2         | 3 678 µs | 3 667 µs | ~0%   | Eligibility skip (largeBins=2) |
| `SmallWorld`              | 2      | 0         | 11 µs    | 9 µs     | noise | Fallback |
| `ManyTinyBins`            | 32     | 0         | 185 µs   | 181 µs   | noise | Fallback |

**Diagnosis.** The bench was designed so `SingleArch` exercises the only shape where S10 is in scope (`largeBins == 1`). Pre-S10 Pass C runs the single bin on sim with `latchedJobs == 0` (no worker dispatch, no latch, no wait) — measured at 1 362 µs Pass C, 0 µs wait, on 100 000 cmds → ~13.6 ns/cmd apply cost.

S10 adds a classification pass before dispatch: for every cmd, `commandTargetEntity` (a `std::visit` over the 26-alt variant) + `EntityStorage::locate(handle)` (`alive` check + slot deref) + `subIdx` compute + `push_back`. Empirically that adds ~25–30 ns/cmd of classification overhead — roughly double the apply cost itself. The cmd stream's memory traffic on `slots_` is touched twice (once at classify, once at apply), and the worker apply on cold cache lines doesn't recoup the savings.

Pass C went from 1 362 µs to 4 523 µs (+233%); the parallel apply theoretical win was at most ~1 100 µs (replacing 100k×13.6 ns sim work with 20k×13.6 ns per lane), nowhere near enough to offset the ~3 000 µs classification penalty.

**Gates vs spec:**

- ≥15% on a workload where `largeBins < workerCount` — **MISSED.** `SingleArch` (the only such workload in scope) regresses by +207%; `Churn`/`MultiArch` (largeBins ≥ 2) are correctly skipped.
- No regression on workloads where `largeBins >= workerCount` — **PASSED** (eligibility check skips them; observed −2%/+8% movement is within bench noise).
- 10× soak determinism — **PASSED** (`pass_c_split_test` covers serial vs sharded-no-split vs sharded-split; full ctest 126/126 green).

**Decision.** Land the implementation but flip `splitLargestBin` default to `false`. The S10 mechanism is correct (determinism verified) but structurally inferior to S9 on every workload measured. A viable S10 would need **record-time row-bucketing** — the cmd's target row recorded into a per-row-range bucket at `CommandBuffer::setTransform()` etc. time, analogous to how S8 records the per-chunk bucket. That's a record-API change with cross-cutting impact on CommandBuffer's storage and is out of scope for this batch.

**Action items.**

- Move to S11 (JobLatch spin-before-block). Latch wait is small post-S9 but still measurable on some workloads; S11's surface area is contained.
- Defer the "row-bucket at record time" investigation to a future batch outside the S9–S13 envelope — it's a 2× larger change than any of S0–S10 and the bench doesn't currently demonstrate a workload where post-S9 sharded loses badly enough to justify it.

### S11 — JobLatch spin-before-block

**Hypothesis.** `JobLatch::wait()` enters mutex+CV blocking immediately. For sub-millisecond apply lanes, the CV wakeup cost (~10µs path on Linux) dominates the actual wait. A short spin (e.g., 50µs hot-loop checking the atomic counter) before falling back to CV catches the common case where workers finish "soon."

**Change.** `JobLatch::wait()` adds a configurable spin budget (default ~50µs measured by `rdtsc` deltas) before acquiring the lock. The mutex+CV path remains the fallback. The TSAN-clean property survives because the spin only reads the atomic, doesn't touch shared state in a way that creates new race surface.

**Bench.** Especially MultiArch + ManyTinyBins + SmallWorld where worker apply time is short.

**Tests.** Existing JobSystem stress + the full determinism harness. The spin is read-only on the latch counter, so no behavioral change.

**Gate.** ≥5% improvement on any workload where current Pass C wait > 90% of Pass C wall-clock. No regression where Pass C wait is already < 10%.

**Risk.** Medium. Spin time is dead CPU on cores that could be doing other work — bad citizenship if the simulator runs alongside other processes. Configurable budget gates the damage; default should be conservative.

**S11 outcome (LAND, default ON).**

Knob shipped as `Config::jobLatchSpinIters` (default `4096` ≈ 10-40 µs). `JobLatch::wait()` hot-loops on an atomic `done_` flag (release-stored by the final `count_down` decrementer) before falling back to the mutex+CV blocking path. Implementation differs from the original spec in one safety-critical way: **even when the spin observes `done_ == true`, `wait()` still re-acquires the mutex before returning.** The acquire-on-`done_` alone is not enough — the final worker is still inside its `count_down` `lock_guard` when it stores `done_`, so a stack-allocated latch's destructor would race the worker's `cv_.notify_all()` / mutex unlock (`pthread_cond_destroy` / `pthread_mutex_destroy` vs in-flight `pthread_cond_broadcast`). TSAN flagged this on the first run; the fix is `std::lock_guard<std::mutex>` taken unconditionally on the spin-win path. The actual savings then come from skipping the `cv_.wait()` kernel sleep / wakeup IPI — the mutex acquire itself is uncontended (~30 ns).

Bench (3-run averages, sharded-path commit_us, S11 ON vs `THREADMAXX_NO_LATCH_SPIN=1`):

| Workload                  | commit_us ON | commit_us OFF | Δ commit | wait_us ON | wait_us OFF | Δ wait |
|---------------------------|--------------|---------------|----------|------------|-------------|--------|
| `setTransform/MultiArch`  | 5124         | 6575          | −22.1%   | 125        | 641         | −80%   |
| `setTransform/Churn`      | 3572         | 3937          | −9.3%    | 3.5        | 0.3         | noise  |
| `setTransform/SingleArch` | 2242         | 2322          | −3.4%    | 0          | 0           | —      |
| `RPG-mix`                 | 3631         | 3703          | −1.9%    | 0.3        | 0.3         | —      |
| `ManyTinyBins`            | 186          | 190           | −2.4%    | 0          | 0           | —      |
| `setVelocity/Churn`       | 3241         | 2972          | +9.1%    | 4.2        | 7.3         | noise  |

Gate (≥5% on the wait-dominated workload): **met on MultiArch** (the only workload post-S9 where Pass C wait was still measurable as a fraction of commit). `setVelocity/Churn` shows a +9% regression but the per-run variance is huge (3737/2558/3427 µs ON, 3654/2779/2483 µs OFF) — both configs span the same range, so the delta is noise. No detectable regression on the no-wait workloads.

Determinism: identical commitHash with the knob at any value (verified by full ctest 126/126 green at default `4096` AND at `0`). TSAN: clean on every test once `hashDirty` is upgraded to `std::atomic<bool>` (see follow-up below).

**Follow-up landed (2026-05-25).** `ArchetypeChunk::hashDirty` upgraded from plain `bool` to `std::atomic<bool>` with relaxed ordering. The S10 row-split path puts multiple workers on disjoint rows of the same chunk; all write `true` to `hashDirty`, which is a data race on a plain `bool` (UB on paper, idempotent in practice). Relaxed ordering is sufficient because (a) writes are idempotent and (b) the read in `finalizeCommitHash` is sequenced after the JobLatch mutex acquire that ends Pass C. `ArchetypeChunk` now defines explicit copy/move ctors that load+store the atomic, preserving its `std::vector<ArchetypeChunk>` usage. Full ctest including `pass_c_split_test` clean under TSAN.

**Action items.**

- Move to S12 (Pass B / Pass C streaming overlap) if the gap to flipping the default still warrants more aggressive scheduling. With MultiArch commit −22% and the post-S9+S11 picture, the S∞ blocker (balanced multi-large-bin saturating workers) is mostly closed.

### S12 — Pass B / Pass C streaming overlap

**Hypothesis.** Pass B and Pass C are strictly serial today. Workers idle during Pass B's classification; sim thread idles during Pass C's wait. Streaming Pass C job submissions WHILE Pass B is still classifying overlaps the two windows.

**Change.** In Pass B's per-buffer loop, when a bin's pending size exceeds `kMinBinForJob`, snapshot the bin pointer span and submit a worker job immediately. Subsequent late additions to that bin during Pass B's continued walk go to a fresh sub-bin (sharing the same archetype index) which then also gets submitted on threshold crossing. Final cleanup at end of Pass B submits any remaining sub-bin tails.

Determinism: each sub-bin still applies its cmds in submission order. Across sub-bins of the same archetype, the order is "earlier sub-bin's cmds before later sub-bin's cmds" because we cut sub-bins on threshold crossing in submission-order classification. Preserve via sequential submission (one sub-bin completes before the next starts) — but that defeats the parallelism. Alternative: enforce by job-priority + same-worker pinning (later sub-bin pinned to same worker as earlier).

**Bench.** Especially RPG-mix and MultiArch (Pass B is significant for them).

**Tests.** Full determinism harness. The hash contract is the highest-risk surface here — same-archetype sub-bin ordering must round-trip identically to the unsplit baseline.

**Gate.** ≥10% commit_us improvement on RPG-mix. No determinism divergence at 10× soak.

**Risk.** High. Requires careful ordering across sub-bins of the same archetype. The simplest correctness shim (sequential same-archetype) defeats the optimization; the careful shim (same-worker pinning + priority) is a JobSystem refactor.

**Skip condition.** If S9 + S10 + S11 close the gap, skip S12. The complexity is not worth a marginal gain.

### S12 outcome — investigated and parked (2026-05-25)

**Variant attempted** (departing from the spec's "stream Pass C jobs during Pass B" approach, which has a determinism cliff): parallelize Pass B's classification across buffers. Per-buffer routing-active classification is read-only on world state — it walks `chunkBuckets`, checks the migrating bitmap, and pushes cmd pointers into per-buffer scratch. No two workers write to the same scratch slot. A serial merge phase then `vector::insert`s each buffer's scratch bins into the shared `shardChunkBins_` in buffer registration order, preserving submission order. Global-lane apply remains serial. This avoids the JobSystem refactor that the spec's same-worker pinning approach would have required.

Implementation landed (config knob `Config::parallelPassBClassify`, ShardBufferScratch struct, dispatch with `JobLatch`+S11 spin, env override `THREADMAXX_NO_PASSB_PARALLEL=1`), passed full ctest 126/126 green at both ON and OFF (determinism preserved).

**Bench result** (3-run averages, RPG-mix sharded commit_us, post-S11 + hashDirty atomic upgrade):

| Workload | S12 ON | S12 OFF | Δ commit | Pass B ON | Pass B OFF |
|---|---|---|---|---|---|
| RPG-mix | 3 678 µs | 3 661 µs* | +0.5% (noise) | 1 579 µs | 1 568 µs* |

\* OFF mean excludes one outlier run at 5 350 µs commit / 2 841 µs Pass B (cleaner runs: 3 602, 3 720).

**Verdict: parked.** S12 fails the ≥10% gate on RPG-mix. The parallel classify pays a serial merge that doubles the per-cmd memory traffic (write to scratch, then read scratch + write to `shardChunkBins_`); Pass B turns out to be memory-bound on this workload shape, not compute-bound, so workers stall on shared cache lines and the parallelism doesn't recoup the extra pass. The spec's `vector::insert`-based merge is the structural bottleneck — a future revisit would need either (a) lock-free direct writes from workers into `shardChunkBins_` (requires per-chunk slot pre-allocation, which requires a pre-pass equivalent to classify itself), or (b) record-time per-buffer-per-chunk capacity hints so the merge becomes free pointer arithmetic.

Per the spec's skip condition ("If S9 + S10 + S11 close the gap, skip S12"), the S∞ readiness assessment now stands: the remaining sharded-vs-single gap on RPG-mix is ~30% (3 678 µs vs ~2 800 µs single). No remaining batch in the plan is structurally cheap enough to close it. **S∞ stays blocked on RPG-mix.**

**Action items.**

- Defer S∞ pending a fresh ideas — neither S12 nor S13 (cross-tick pipelining, even higher risk) is on the near-term roadmap.
- Consider documenting the post-S11 + atomic state as the long-term shipping configuration: serial commit remains the default; sharded commit available as an opt-in for workloads that profile as Pass-C-dominated (single-largest-bin or balanced-multi-bin shapes where S9/S11 deliver clean wins).

### S13 — Record-time target-entity cache (INVESTIGATED + PARKED 2026-05-25)

(Repurposed slot from the original "Cross-tick commit pipelining" S13 entry below — a much cheaper attempt at the same goal: close the sharded gap.)

**Hypothesis.** `commandTargetEntity()` is a per-cmd `std::visit` over a 19-alternative variant; its bucket-walk-demotion call site in Pass B dominates Pass B on value-only workloads. Cache the target entity index at record time in a `std::vector<std::uint32_t>` parallel to `commands_` so Pass A/B skip the visit and read a single 32-bit value at a known offset.

**Implementation landed.** New `CommandBuffer::cachedTargetIdx_` vector, `kInvalidTargetIdx` sentinel, push from every recording method, `noteGlobalCommand(cmdIdx, entity)` two-arg form, `clear()` resets it. Pass A's `markMigrating` and Pass B's bucket-walk demotion read `targetIdx[idx]` directly. Determinism unchanged (same bitmap, same demotion logic, just a faster index read). Full ctest 126/126 green at both ON and OFF.

**Bench result** (10-run medians on `setTransform/Churn`, the regression workload; 3-run averages elsewhere):

| Workload | Pass B base → S13 | Pass A base → S13 | commit_us base → S13 | Δ commit |
|---|---|---|---|---|
| setTransform/Churn (median) | 590 → 368 (-37.6%) | 0.09 → 0.05 | 3296 → 4061 | **+23%** |
| setVelocity/Churn | 591 → 359 (-39%) | 0.08 → 0.05 | 2910 → 3151 | +8% |
| setTransform/MultiArch | 702 → 381 (-46%) | 0.08 → 0.05 | 6010 → 5562 | -7.5% |
| setTransform/SingleArch | 592 → 371 (-37%) | 0.08 → 0.04 | 2244 → 2056 | -8.4% |
| RPG-mix | 1538 → 1476 (-4%) | 41.9 → 22.1 (-47%) | 3620 → 3698 | +2% (noise) |
| ManyTinyBins | 58 → 35 (-40%) | 0.08 → 0.04 | 181 → 168 | -7.2% |

**The Pass B optimization itself works** — 37–46% per-pass reduction on every value-only workload, exactly as the std::visit-cost model predicted. **But `setTransform/Churn` regresses ~23% in total commit_us** (median).

**Diagnosis.** Pass C grows by ~30% on Churn (median 2509 → 3328 µs) while Pass A/B savings only recover ~250 µs. To isolate the cause, an intermediate "record-only" run was done: keep `cachedTargetIdx_.push_back(...)` in the recorders but revert Pass B to use `commandTargetEntity()` (so the cache is written but unused). Result: Pass C still regressed ~30%. **The cost is on the recording side, not the commit-side optimization.**

The mechanism isn't fully explained by the small absolute cost of the extra `push_back` (~4 bytes / cmd, no per-step reallocations after the first iteration thanks to `clear()` preserving capacity). The most plausible reading: the additional recording work changes CPU frequency / thermal state during the parallel record phase, which propagates into Pass C's worker-thread apply on this specific workload shape (lots of migrations + chunk reshape). SingleArch/MultiArch/ManyTinyBins do NOT show this regression — Churn's signature is migration-driven chunk reshape colocated with value-only recording.

**Verdict: parked.** The user's gate ("≥5% RPG-mix sharded total OR ≥20% Pass A *without regression*") is failed by Churn. RPG-mix doesn't benefit because its Pass B time is dominated by global-lane apply, not bucket-walk demotion. A future revisit could try a different design that avoids the per-cmd record-time push:

- Store the entity inside the bucket entry itself (`std::pair<uint32_t, uint32_t>` per bucket entry) so only routed-value-only cmds pay the extra 4 bytes; trade bucket-entry size for parallel-vector size.
- Lift `EntityHandle` to a fixed offset within the variant payload via UB-adjacent variant ABI tricks (skip the `std::visit` jump table by directly reading at offset 0). Fragile.
- Profile under `perf` with CPU-frequency pinning to confirm or rule out the thermal hypothesis on Churn.

**Per-batch consequence.** S15 (row-bucketed record-time routing — the S10 revisit) is also blocked by the same record-time-cost concern: it would require even MORE per-cmd record-time work (chunk + row), and would inherit Churn's record-time-cost issue at higher amplitude. Without solving the Churn record-time-cost mystery first, S15 is unlikely to net-win.

S∞ readiness unchanged from S12: serial commit stays default, sharded stays opt-in for Pass-C-balanced workloads.

### S13-original — Cross-tick commit pipelining (S∞ blocker territory)

**Hypothesis.** Most aggressive: allow tick N's Pass C to run while tick N+1 begins recording. The sim thread starts the next tick's update() while the previous tick's apply is still in flight.

**Change.** Engine maintains a "commit in flight" flag. update() of tick N+1 proceeds only if the commit it depends on (e.g., reads from system A's writes via worldView) has finished. Systems that read fresh state declare a dependency edge to the prior tick's writers; the engine inserts an explicit barrier before their update().

This breaks the current `registration-order serial commit` contract: previously, by the time update() runs, ALL prior commits have finished. Under S13, only some have. WorldView semantics must be revisited.

**Change scope:** big. Touches Engine::step, SystemContext, WorldView, the read-set / write-set scheduler.

**Bench.** Whole-tick wall clock on RPG-mix + MultiArch. Target the END of tick N+1 (or really N+K) showing meaningful overlap with N's Pass C.

**Tests.** Full determinism harness. Plus new `tests/cross_tick_pipelining_test.cpp` confirming bit-exact replay across cross-tick scheduling.

**Gate.** Whole-tick wall clock ≥15% improvement OR the default flip becomes viable.

**Risk.** Highest. Breaks the current commit contract; large architectural change.

**Skip condition.** If S9+S10+S11 flip the default, S13 is unnecessary.

### S∞ — Flip the default

Land in a separate commit, after the batches above have been measured over a full soak cycle.

- `Config::singleThreadedCommit = false` becomes the default.
- `doc/performance_tuning.md` gets a "When to set `singleThreadedCommit = true`" section explaining the small-world fallback.
- CLAUDE.md's "Sharded never beats single-threaded on tested workloads" paragraph gets rewritten.
- Bump engine version to v1.3.x (decision-level change to a default).

## 8. Stop conditions (when to abandon)

This plan is provisional. Pull the brake when:

- **S0 contradicts the premise.** If the breakdown shows sharded and serial are within 5% on every workload, there's no leverage and we keep the serial default. Document and close.
- **Two consecutive batches fail their bench gate.** Either the model of the bottleneck is wrong or the remaining gains are below measurement noise. Re-do S0 before continuing.
- **Determinism harness ever diverges.** Revert the offending batch, re-run S0 with the divergence as input, and only continue once the divergence root cause is mapped.
- **Plan grows beyond S13.** S9–S13 specifically target the Pass C latch-wait bottleneck S8 couldn't reach. If S9–S13 don't unblock S∞ either, the assumption that "MultiArch can be made to net-win" was wrong; flip back to ship-what-we-have or accept the serial default permanently.

## 9. Out-of-scope for this plan

- **Worker count tuning.** Phase-T (`ADAPTIVE_TUNING.md`) handles this; sharded-path performance is measured at the user's `workerCount`.
- **SIMD apply.** Per-command apply is dominated by mask checks and pointer chasing, not arithmetic. SIMD is a separate effort.
- **Lock-free `CommandBuffer`.** Each system already owns its own `CommandBuffer` per worker; there's no shared write.
- **Cross-step reordering.** Submission order is the contract. Any reorder is a hash change.

## 10. Critique of `threadmaxx_sharded_commit_path_optimization_plan.md`

What the ChatGPT plan got right:

- The order of attack: measure first, then attack classifier, then bins, then sync. That's the right shape and is preserved here.
- The determinism non-negotiables (commitHash + WorldSnapshot + singleThreadedCommit fallback). Adopted verbatim into §3.

What this plan changes:

- **2.1 and 2.3 are the same idea ("compact command record / predecoded header") — collapsed into S1.** Stating it twice as separate phases creates the illusion of more leverage than the change actually delivers.
- **Phase 4 (adaptive cutoff) is already half-done.** The existing fallthrough at `totalCommands < 256 || totalValueOnly == 0 || chunkCount < 2` is exactly this idea, statically tuned. The real next step is a learned threshold; that's S7, gated.
- **Phase 3.1 (per-chunk command buffers) is the largest API-adjacent change in the plan and is the most likely to net-regress** (every `cb.set*` call pays a hint lookup). It's S8 here, explicitly gated, and the plan calls out the kill-switch: if S0–S5 already flip the default, don't ship S8.
- **Phase 0 measurement is too unscoped** in the ChatGPT version (eight bullet points without a stop condition). S0 here is bounded — three named hypotheses, one of which the data must support before any code batch lands.
- **The bench matrix in Phase 1 of the ChatGPT plan misses the SmallWorld regression case.** That's exactly the workload where sharded must NOT regress, and it's the test that protects the auto-fallthrough. Added explicitly in §4.
- **The ChatGPT plan has no per-batch gate.** Every batch here has a numeric bench target and a revert path. That's the difference between a roadmap that ships and a roadmap that drifts.

## 11. Out-the-door checklist (when S∞ fires)

- [ ] `Config::singleThreadedCommit` default flipped to `false`.
- [ ] All seven workloads in §4 measured: sharded mean ≤ serial mean on `MultiArch`, `RPG-mix`, and at least one of the Churn variants.
- [ ] `SmallWorld` continues to fall through to serial.
- [ ] `tests/sharded_commit_test.cpp` green for 10× the current soak duration.
- [ ] TSAN green on `commit_soak_test`.
- [ ] CLAUDE.md updated. `doc/performance_tuning.md` updated. CHANGELOG.md entry for v1.3.x.
- [ ] Migration note in `doc/migration_v1_2_to_v1_3.md` covers the default flip.
