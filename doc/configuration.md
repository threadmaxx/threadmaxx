# Configuration & Lifecycle

@page configuration Configuration & Lifecycle

## `Config`

`Config` is a plain struct of knobs. Defaults are sensible for an
interactive 60 Hz game; tests override `sleepToPace = false` and pick a
fixed `workerCount` for reproducibility.

### Core knobs

| Field | Default | Meaning |
| --- | --- | --- |
| `workerCount` | `0` | Number of worker threads. `0` = `max(1, hardware_concurrency - 1)`. The "minus one" leaves the simulation thread its own core. |
| `fixedStepSeconds` | `1.0 / 60.0` | Fixed simulation step. The engine never deviates from this within a single `step()`. |
| `maxStepsPerIteration` | `8` | In `run()`, an upper bound on how many `step()` calls per outer iteration when catching up to wall-clock. Prevents the "spiral of death" if a frame takes longer than the step. |
| `deterministic` | `false` | When `true`, the engine guarantees the same world state for the same inputs across runs and machines. Today this is mostly already true; the flag is a documentation of intent. |
| `sleepToPace` | `true` | `run()` sleeps to match wall-clock. `false` iterates as fast as possible (tests, offline). |
| `initialEntityCapacity` | `1024` | Hint for the dense storage. Storage grows past this on demand. |

### Commit-path knobs

These control the commit phase (`commitBuffer` / `commitBuffersSharded`).
See [Performance tuning](performance_tuning.md) for guidance on when to
flip them and the bench data behind each default. The sharded-path
batches (S6, S8, S9, S10, S11, S16) trace back to
`SHARDED_OPTIMISATION.md` at the repo root.

| Field | Default | Meaning |
| --- | --- | --- |
| `singleThreadedCommit` | `true` | Run the commit phase serially on the sim thread (the deterministic reference). `false` opts into `commitBuffersSharded` (Pass A / Pass B / Pass C). Bit-for-bit identical state either way (`EngineStats::commitHash` is the gate). Sharded loses on every measured workload today; documented as the fallback for profile-confirmed contention. |
| `logCommitHashEvery` | `0` | When `N > 0`, the engine logs `commitHash tick=<T> hash=0x<hex>` via `ILogger@Info` every `N` ticks. Used for incident-response divergence chase; zero cost when `0`. |
| `legacyCommitHash` | `false` | When `false` (the v1.3 contract default), `commitHash` is the per-archetype state rollup. `true` restores the v1.x byte-mix path for the migration window — `[[deprecated]]` and slated for removal once the v1.3 floor is shipped. See [Migration v1.2 → v1.3](migration_v1_2_to_v1_3.md). |
| `batchMigrateThreshold` | `16` | **S6 — migration batching.** Per-buffer run-length at which contiguous same-`(srcArch, dstMask)` mask-change runs dispatch through `setMaskAndMigrateBatch`. Set to `numeric_limits<uint32_t>::max()` to fully disable batching (used by `bench/migration_bench` for A/B). |
| `recordTimeRouting` | `true` | **S8 — record-time per-chunk routing.** Sharded-on only. Value-only setters bin into `chunkBuckets_` at record time so Pass A consumes only the migrating-index list (not the full command stream). Ignored when `singleThreadedCommit = true`. |
| `inlineLargestBin` | `true` | **S9 — sim-thread-inline largest bin.** Sharded-on only. Pass C runs the single largest large-bin inline on sim; `largeBins − 1` worker jobs go to `JobLatch`. Sim becomes a peer lane → latch wait ≈ 0 on routing-active workloads. Ignored when `singleThreadedCommit = true` or `largeBins == 0`. |
| `splitLargestBin` | `false` | **S10 — row-split largest bin (OPT-IN, parked).** Pass C row-partitions the largest large-bin into sub-bins. Default off because the classifier cost exceeds the apply cost on tested workloads; the partitioner + test (`tests/pass_c_split_test.cpp`) are preserved as the fixed point for a future record-time row-bucketing classifier. |
| `jobLatchSpinIters` | `4096` | **S11 — JobLatch spin-before-block.** `JobLatch::wait()` spins on an atomic "done" flag for up to this many iterations before falling back to mutex+CV. ≈10-40 µs spin budget — saves the kernel-sleep / wakeup-IPI cost (~5-15 µs) when workers finish within the window. Set to `0` for the legacy mutex+CV-only path. |
| `workloadAwareCommit` | `false` | **S16 — workload-aware auto fallthrough (OPT-IN).** When `true`, `commitBuffersSharded` falls through to the serial commit path whenever the global-command fraction meets `workloadAwareGlobalPercent / 100`. Lets the engine pick single-vs-sharded per call from cheap counters. Ignored when `singleThreadedCommit = true`. |
| `workloadAwareGlobalPercent` | `30` | Threshold consumed by `workloadAwareCommit`. RPG-mix-shaped workloads (≈50% global) trip it; `setTransform` variants (≈0% global) sail through to sharded. Ignored when `workloadAwareCommit = false`. |

## Lifecycle

```
   Engine engine(cfg);
       │
       ├─ initialize(game)
       │      │ build World, spawn workers, instantiate ResourceRegistry
       │      │ call game.onSetup(engine, world, seed)
       │      │ commit `seed` before the first tick
       │      ▼
       ├─ run()  (or repeated step())
       │      │ loop: catch-up step() calls, then submitInterpolatedFrame
       │      │ exits when quitRequested() is true
       │      ▼
       └─ shutdown()
              │ call game.onTeardown(engine, world)
              │ submit final RenderFrame
              │ call renderer.shutdown()
              │ call each system's onUnregister(world)
              │ drain workers, destroy systems, drop world
              ▼
   (engine destructor)
```

Both `run()` and `step()` are safe to call directly. `step()` is the
primitive: one fixed step, no pacing, no interpolation pass. `run()`
wraps it in a loop with wall-clock pacing.

`shutdown()` is **idempotent**: calling it twice is fine, and the
destructor calls it for you if you forget. Calling `step()` or `run()`
after `shutdown()` is a no-op.

`requestQuit()` is thread-safe — it's the only `Engine` method besides
`quitRequested()` that you can call from a non-sim thread while `run()`
is executing. The console example wires it to SIGINT.

## Custom loops

If you want to drive timing yourself (e.g. for a server that pulls
network packets per tick, or a tool that needs a deterministic step
count):

```cpp
while (!engine.quitRequested()) {
    pumpNetwork();
    engine.step();
    drainEvents();
}
```

In this mode `Config::sleepToPace` and `Config::maxStepsPerIteration`
are not consulted — you're driving the loop. The fixed-step contract
still holds: every `step()` advances exactly one tick.

## Deterministic mode

`Config::deterministic = true` is currently a declaration of intent
rather than a code path. The commit-in-submission-order rule and the
single-threaded commit phase already make the world state reproducible
under fixed system order; the flag is reserved for future "fail loud if
non-determinism is detected" checks (e.g. asserting workers don't read
the wall clock).

`tests/determinism_test.cpp` verifies that two engines with the same
seed and inputs produce identical entity state over many ticks.

## Multi-threading defaults

`workerCount = 0` picks `hardware_concurrency - 1`. The reasoning: the
simulation thread is one of the threads contending for CPU, and it
matters that it can keep up with worker output. Leaving it a core to
itself is more important than squeezing one more worker out.

For benchmarks or worst-case stress, pin to a specific count:

```cpp
threadmaxx::Config cfg;
cfg.workerCount = 4;
```

## Step rate

`fixedStepSeconds` is the engine's unit of time. Everything is fixed-
step: physics, AI, animations. Render-side interpolation (via
`RenderFrame::alpha`) handles the gap between sim ticks for smooth
visuals.

To switch to 120 Hz: `cfg.fixedStepSeconds = 1.0 / 120.0`. Be aware that
every system's per-step cost doubles — the engine doesn't auto-tune.

The fixed step is not changeable mid-run. Pause and time-scale are
first-class on the engine itself: `setPaused(bool)` makes `step()` a
no-op while keeping the render submit live; `setTimeScale(double)`
multiplies the effective `dt` (zero is clamped, see
[Pause & time scale](pause_and_time_scale.md)).
