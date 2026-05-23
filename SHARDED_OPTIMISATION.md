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
- **Plan grows beyond S8.** If we're inventing batches past S8, the plan was wrong. Stop and either flip the default with the wins we have, or back out and live with the serial default.

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
