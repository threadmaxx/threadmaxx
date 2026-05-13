# Lifecycle hooks

@page lifecycle_hooks Lifecycle hooks

A `Engine::step()` call walks three serial phases plus the wave loop in
between:

```
preStep (registration order)
  → wave 0 (parallel) → commit
  → wave 1 (parallel) → commit
  → ...
postStep (registration order)
  → tick++, render frame published
```

`update()` is the wave-scheduled hook you've already met — it runs
concurrently with sibling systems whose declared reads/writes don't
conflict, with work fanned out via `parallelFor`. The other two hooks
on `ISystem` run **single-threaded on the simulation thread** and
**serially across systems** in registration order.

## When to use which hook

| Hook         | Phase                    | Concurrency       | Typical use |
|--------------|--------------------------|-------------------|-------------|
| `preStep`    | before any wave runs     | serial, sim thread | drain input queue, snapshot last tick's state, reset per-tick accumulators |
| `update`     | wave (potentially parallel) | parallel jobs     | the meat of the system — read state, record `CommandBuffer` mutations |
| `postStep`   | after the last commit    | serial, sim thread | publish events, refresh a HUD, finalize aggregates |
| `onRegister` | once at registration     | sim thread         | one-shot setup; cache a resource handle |
| `onUnregister` | once at shutdown       | sim thread         | one-shot teardown |

`preStep` and `postStep` are virtual with empty defaults — systems that
don't override them pay one virtual call per tick (negligible). They
both receive a `SystemContext&` so you can record commands via
`ctx.single([&](Range, CommandBuffer& cb){ ... })`; those commands are
committed in registration order, immediately for `preStep` (so the
wave-phase systems see them) and at the same point in the tick for
`postStep` (so the next tick's `preStep` sees them).

## Why three hooks?

The wave scheduler buys parallelism by allowing non-conflicting systems
to run together. That breaks workflows that want a deterministic
"pre-tick fanout" or "post-tick fanin" point — `preStep`/`postStep` give
you back that serial slot without sacrificing the wave model. They are
also the right place to bridge across the engine boundary: pumping an
external network queue, reading a player input device, or pushing a
batch of trace events to telemetry.

## Example

```cpp
class InputSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "input"; }

    void preStep(threadmaxx::SystemContext& ctx) override {
        // Pump the OS-side input queue into ECS state once per tick,
        // so wave-phase systems see a consistent snapshot.
        ctx.single([this](threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
            for (auto evt : drainNativeInput()) cb.setUserData(evt.entity, ...);
        });
    }

    void update(threadmaxx::SystemContext& ctx) override {
        // The wave-phase: react to the input we just published.
        threadmaxx::forEach<threadmaxx::UserData>(ctx, /*...*/);
    }

    void postStep(threadmaxx::SystemContext&) override {
        // Snapshot last-tick stats for a debug overlay.
        debugHud_.snapshot(ctx.tick(), debugCounter_);
    }
};
```

## Ordering guarantees

- All `preStep` callbacks fire in registration order before any wave
  starts.
- All `postStep` callbacks fire in registration order after the last
  wave commits.
- Within `preStep`/`postStep` you cannot rely on `parallelFor` to gain
  parallelism with sibling systems — those calls still go through the
  worker pool, but the sim thread waits for them before moving on.
  `single()` is the natural primitive here.
