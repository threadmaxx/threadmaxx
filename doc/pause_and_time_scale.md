# Pause and time scale

@page pause_and_time_scale Pause and time scale

Two engine-level knobs that affect simulation speed without touching
the wall-clock pacing loop:

```cpp
engine.setTimeScale(0.5);    // half-speed: dt seen by systems is fixedStepSeconds * 0.5
engine.setTimeScale(2.0);    // double-speed
engine.setTimeScale(1.0);    // normal

engine.setPaused(true);      // step() becomes a no-op; run() still draws
engine.setPaused(false);     // resume
```

Both are safe to call from any thread (in practice you call them from
your game/UI code, often via input handlers).

## What time scale changes

- The `dt` value systems see via `SystemContext::dt()` is
  `Config::fixedStepSeconds * timeScale`. Movement, integration,
  cooldown timers — anything that multiplies by `dt` — slows down or
  speeds up proportionally.

## What it does NOT change

- `Engine::tick()` still increments by exactly 1 per `step()`.
- `Engine::simulationTime()` still advances by `fixedStepSeconds` per
  step.
- The fixed-step wall-clock pacing is unchanged: `run()` still issues
  `step()` calls at the same rate.

This separation is what makes time-scale composable with deterministic
mode: the tick stream stays a clean integer, and time-scale lives
entirely in user space.

## Negative scale

`setTimeScale(s)` clamps `s` to zero if negative. There is no
"backwards time" mode — the engine is forward-only.

## Pause semantics

When paused, `step()` returns immediately without running any system.
Stats are zeroed for the skipped step (so a HUD doesn't show stale
work). `run()` keeps the render-frame submission going — the same
front-buffer is re-submitted every iteration, so the renderer doesn't
freeze. This is what you want for pause menus: the camera can still
move, post-processing can still animate, but the simulation is
frozen.

If you need to advance one frame at a time while paused (debug
step-mode), unpause, call `step()` once, then re-pause.

## Substepping

Substepping (running multiple fixed steps per render frame to keep
physics stable at low FPS) is a separate, larger lift and is parked.
The current model is "one render frame = at most `maxStepsPerIteration`
fixed steps" set by `Config::maxStepsPerIteration` and the wall-clock
catch-up loop.
