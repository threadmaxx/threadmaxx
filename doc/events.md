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
jobs and engine-owned helper threads (the stall watchdog uses it).
Since batch 13c the back buffer is a **lock-free MPSC Treiber stack**:
each `emit` allocates a node and CAS-prepends to an atomic head; on
drain the engine atomically detaches the whole stack and reverses it
into the read buffer to restore per-thread FIFO order. There is no
mutex on the hot emit path — concurrent producers contend only on
the head pointer's CAS, never on a lock.

Ordering: within a single producer thread, emit order is preserved
across drain. Across producer threads the interleaving is undefined
— concurrent emitters have no synchronization between them anyway.
Subscribers fire in the same order `drainTick` would yield.

`drainTick` returns a `span` into the front buffer (no copy); the
front buffer is single-writer-multi-reader during a tick, so any
system whose `update` reads via `drainTick` is safe even if other
systems read the same channel concurrently.

`pendingCount()` is now an atomic counter rather than a vector
size; it is approximate under concurrent emit and intended for
tests and HUD readouts, not for hot-path branching.

## What about the same-tick case?

The double-buffer is a deliberate design choice: it gives you a stable,
consistent view across the tick. If you genuinely need same-tick
delivery, your producer and consumer should be in the same system (or
should communicate via a `CommandBuffer` mutation that the consumer
reads via `World` next tick).

## Persistent subscriptions

`EventChannel<T>::subscribe(fn)` registers a callback that fires once
per emitted event at the tick boundary, before the buffer flips. Pair
with `unsubscribe(id)` to detach. This is sugar on top of the
`drainTick()` loop — useful when several non-system observers (HUDs,
loggers, replays) want to watch the same channel without each having
to write its own consumer system.

```cpp
auto& chan = engine.events<DamageEvent>();

// Register a callback. Returns a non-zero id.
const auto id = chan.subscribe([](const DamageEvent& e) {
    std::printf("damage: %d\n", e.amount);
});

// ... later, in shutdown logic or when no longer interested:
chan.unsubscribe(id);
```

Callbacks fire on the simulation thread during the tick-end drain.
They run to completion before the front/back swap, so a callback that
re-emits sees its own events on the next tick (the same one-tick
delay that `drainTick()` consumers see).

Subscriptions and `drainTick()` consumers coexist — installing a
subscriber does not steal events from `drainTick`.

## RAII subscriptions

`EventChannel<T>::subscribeScoped(fn)` is the §3.2 batch-7 sugar for
`subscribe` + manual `unsubscribe`. It returns a `Subscription` handle
(type-erased; move-only) that auto-detaches on destruction:

```cpp
class HudOverlay {
public:
    HudOverlay(threadmaxx::Engine& engine)
        : sub_(engine.events<DamageEvent>().subscribeScoped(
              [this](const DamageEvent& e) { onDamage(e); })) {}
    // No explicit unsubscribe needed; ~Subscription detaches.
private:
    void onDamage(const DamageEvent&);
    threadmaxx::Subscription sub_;
};
```

The handle is **safe to outlive the channel.** Internally it holds a
`weak_ptr` to the channel's subscriber list, so if the engine is
destroyed before the `Subscription`, the eventual destructor no-ops
instead of dereferencing a dangling pointer. This matters when
`Subscription`s live in long-running game objects that may not be
torn down in a precise order.

```cpp
threadmaxx::Subscription sub;
{
    threadmaxx::Engine engine(cfg);
    /* ... */
    sub = engine.events<Bang>().subscribeScoped([](const Bang&) { });
    /* engine destroyed here */
}
// sub is now invalid — sub.valid() returns false — and its
// destructor is a no-op when it eventually fires.
```

Use `Subscription::reset()` to detach eagerly without destroying the
handle, and move semantics (`Subscription a = std::move(b);`) to
transfer ownership. Copy is intentionally disabled — duplicating a
subscription is almost never the right thing.

If you need the bare numeric id (e.g. for interop with a callback
registry that already keys on `SubscriptionId`), the legacy
`subscribe` / `unsubscribe` API is unchanged; the two APIs target the
same underlying list.
