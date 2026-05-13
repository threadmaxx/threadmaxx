# Event channels

@page events Event channels

Typed double-buffered queues for cross-system messaging. Producers
append to the back buffer; consumers read the previous tick's events
via `drainTick()`; the engine flips the buffers on the tick boundary.

## When to use them

- **Combat hits, projectile collisions, area-of-effect triggers.** A
  physics or AI system detects the hit and emits a `DamageEvent`; a
  separate combat-resolution system reads them next tick and applies
  the consequences.
- **Quest triggers, faction-state changes.** Tick-T detection in the
  world; tick-T+1 reaction by quest/UI systems.
- **Animation/audio cues fired from gameplay logic.** Decouple the
  audio system from the gameplay code that knows when a sound should
  fire.

If you find yourself reaching for an `std::queue<Foo>` member on a
system, an event channel is what you want — it's lock-safe under
parallel emit and the lifecycle is engine-managed.

## The API

```cpp
struct DamageEvent {
    threadmaxx::EntityHandle target;
    std::int32_t amount;
};

// Get (or lazily create) the engine-owned channel for this type.
auto& chan = engine.events<DamageEvent>();

// Producer side, from anywhere (including worker jobs):
chan.emit(DamageEvent{target, 50});

// Consumer side, from inside an ISystem::update():
auto events = chan.drainTick();   // std::span<const DamageEvent>
for (const auto& e : events) applyDamage(e.target, e.amount);
```

The channel is keyed by the event type — `engine.events<DamageEvent>()`
returns the same instance everywhere. Different event types have
different channels.

## Lifecycle

Emit-on-tick-T → visible-via-drainTick on tick T+1.

```
tick T:   update() — Producer.emit(...)       → back buffer accumulates
          end of step: engine drains          → swap front<->back
tick T+1: update() — Consumer.drainTick()     → reads what was emitted on tick T
          end of step: engine drains          → swap, clearing T+1's emits into front
```

The implication: a system can both emit and read from the same channel
in the same `update()`. The read returns the *previous* tick's events,
not the ones the system just emitted. This avoids feedback loops by
construction.

## Thread safety

`emit` is safe to call from any thread, including parallel worker
jobs. Internally it locks a single per-channel mutex on the back
buffer; contention is bounded by emit frequency, which is typically
small. `drainTick` returns a `span` into the front buffer (no copy);
the front buffer is single-writer-multi-reader during a tick, so any
system whose `update` reads via `drainTick` is safe even if other
systems read the same channel concurrently.

## What about the same-tick case?

The double-buffer is a deliberate design choice: it gives you a stable,
consistent view across the tick. If you genuinely need same-tick
delivery, your producer and consumer should be in the same system (or
should communicate via a `CommandBuffer` mutation that the consumer
reads via `World` next tick).
