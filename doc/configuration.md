# Configuration & Lifecycle

@page configuration Configuration & Lifecycle

## `Config`

`Config` is a plain struct of knobs. Defaults are sensible for an
interactive 60 Hz game; tests override `sleepToPace = false` and pick a
fixed `workerCount` for reproducibility.

| Field | Default | Meaning |
| --- | --- | --- |
| `workerCount` | `0` | Number of worker threads. `0` = `max(1, hardware_concurrency - 1)`. The "minus one" leaves the simulation thread its own core. |
| `fixedStepSeconds` | `1.0 / 60.0` | Fixed simulation step. The engine never deviates from this within a single `step()`. |
| `maxStepsPerIteration` | `8` | In `run()`, an upper bound on how many `step()` calls per outer iteration when catching up to wall-clock. Prevents the "spiral of death" if a frame takes longer than the step. |
| `deterministic` | `false` | When `true`, the engine guarantees the same world state for the same inputs across runs and machines. Today this is mostly already true; the flag is a documentation of intent. |
| `sleepToPace` | `true` | `run()` sleeps to match wall-clock. `false` iterates as fast as possible (tests, offline). |
| `initialEntityCapacity` | `1024` | Hint for the dense storage. Storage grows past this on demand. |

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

The fixed step is not changeable mid-run. If you need pause / time-scale,
the right shape is a wrapper around `step()` in your game's outer loop;
the engine itself does not yet have first-class time-scale support
(future work item).
