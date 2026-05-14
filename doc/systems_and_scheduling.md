# Systems & Scheduling

@page systems_and_scheduling Systems & Scheduling

## What a system is

A system is a `class : public ISystem` that the engine calls every fixed
step. The minimum:

```cpp
class MovementSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "movement"; }
    void update(threadmaxx::SystemContext& ctx) override { /* ... */ }
};
```

Optional overrides:

- `void onRegister(World&)` — called once after the world is built.
- `void onUnregister(World&)` — called once at engine shutdown.
- `ComponentSet reads()  const noexcept` — declare read set (see below).
- `ComponentSet writes() const noexcept` — declare write set.

`name()` is a const-char pointer the engine *does not copy*. Return a
string literal or otherwise stable-lifetime string.

## What a system can do

Inside `update()`, the system uses `SystemContext` to schedule work:

```cpp
ctx.parallelFor(count, grain, [&](Range r, CommandBuffer& cb) {
    // ... runs on a worker thread, gets a fresh CommandBuffer ...
});

ctx.single([&](Range, CommandBuffer& cb) {
    // ... runs on the sim thread, single-threaded ...
});
```

`SystemContext` also exposes:

- `world()` — const reference to the world for read-only access.
- `dt()` — fixed-step delta in seconds.
- `tick()` — monotonically increasing tick counter.

`parallelFor(count, grain, fn)` splits `[0, count)` into chunks of
~`grain` items each and submits one job per chunk to the worker pool.
Pass `grain = 0` to let the engine pick. The call blocks until every job
has finished.

`single(fn)` runs immediately on the simulation thread. It exists for the
cases where parallelism is genuinely the wrong tool — small fixed work,
operations that must observe the whole world at once, or registration of
listeners.

## Read/write sets and waves

A system declares which component categories it touches:

```cpp
ComponentSet reads() const noexcept override {
    return Component::Transform | Component::Velocity;
}
ComponentSet writes() const noexcept override {
    return Component::Transform;
}
```

The defaults are `ComponentSet::all()` for both, which forces strict
registration-order serial execution. Overriding them lets the engine pack
non-conflicting systems into the same *wave* and run them concurrently.

The conflict rule between systems A and B:

```
conflict(A, B) ⇔ (A.writes ∩ B.writes) ∪
                 (A.writes ∩ B.reads)  ∪
                 (A.reads  ∩ B.writes) ≠ ∅
```

In words: two systems may share a wave only if neither writes a category
the other reads or writes. The engine's wave packer is a greedy
first-fit: each newly registered system is placed in the earliest wave
that contains no conflicts.

## Task-graph edges via tags (§3.4 batch 11)

Read/write masks cover the *what* of dependencies (component
intersection); they don't cover the *when* — sometimes two systems
have non-overlapping masks but one must logically run after the
other (a producer-consumer pair where the consumer reads side-state
the producer scattered around). For those cases, systems declare
named tag edges:

```cpp
class PhysicsSystem : public ISystem {
    static constexpr TaskTag kResults{"physics-results"};
    std::span<const TaskTag> provides() const noexcept override {
        return {&kResults, 1};
    }
    // ...
};

class AISystem : public ISystem {
    static constexpr TaskTag kPhysics{"physics-results"};
    std::span<const TaskTag> dependencies() const noexcept override {
        return {&kPhysics, 1};
    }
    // ...
};
```

The engine adds an edge `PhysicsSystem → AISystem` to the DAG: even
if their read/write masks don't overlap, `AISystem` is pushed into
a wave **strictly later than** `PhysicsSystem`. `TaskTag` is a POD
carrying both the string name (used for equality) and a pre-computed
FNV-1a hash (used as a lookup accelerator); name-based equality
means hash collisions are a performance issue, never a correctness
one.

The DAG is rebuilt every `registerSystem` and `registerSystemAt`.
Cycles (A depends on a tag B provides AND B depends on a tag A
provides) are detected and logged via `ILogger`; the engine recovers
by dropping the offending **tag-only** edges (read/write edges are
always preserved). Subsequent ticks still run — the engine never
deadlocks on a malformed graph.

Inspect the resulting graph at runtime with
`Engine::taskGraphSnapshot()`:

```cpp
const auto snap = engine.taskGraphSnapshot();
for (const auto& node : snap) {
    std::cout << "system " << node.index << " '" << node.name
              << "' is in wave " << node.wave;
    if (!node.dependsOn.empty()) {
        std::cout << " (after: ";
        for (auto p : node.dependsOn) std::cout << p << ' ';
        std::cout << ')';
    }
    std::cout << '\n';
}
```

`taskGraphSnapshot` is sim-thread-only and returns owned strings, so
it's safe to keep across registrations.

## Per-system grain hints (§3.4 batch 11)

`parallelFor(count, grain, fn)` picks the slice size when the
caller passes `grain == 0`. The default heuristic targets
`workers * 4` chunks per call (4 chunks per worker). When a system
*knows* its inner-loop cost — typically because the body does
~constant work per item and there's a sweet spot for cache locality
— override `ISystem::preferredGrain()` and the engine uses your
value for that system's `grain=0` calls:

```cpp
class IntegrationSystem : public ISystem {
    std::uint32_t preferredGrain() const noexcept override {
        return 256;   // tuned offline; 256 items per worker
    }
    // ...
};
```

Pass-through `grain > 0` calls are unaffected — `preferredGrain()`
is only consulted when the user defers to the engine.

## Tick budgets, skipping, and `shouldYield` (§3.5 batch 12)

Some systems are nice-to-have: analytics, debug overlays, low-priority
AI tickers. Missing one tick of those is strictly better than a frame
hitch. The engine has an opt-in skip machinery for exactly this case:

```cpp
class AnalyticsSystem : public ISystem {
    bool skippable() const noexcept override { return true; }  // opt-in
    // ...
};
```

```cpp
engine.setTickBudget(0.012);   // 12 ms per tick (e.g. 60 fps with margin)
```

When the engine's `step()` elapsed time exceeds the budget at a wave
boundary, all subsequent waves' `skippable()` systems have their
`update()` skipped on the current tick. `preStep` / `postStep` /
`buildRenderFrame` are NEVER skipped.

User systems can poll the over-budget flag from inside `update()`:

```cpp
void update(SystemContext& ctx) override {
    for (Range r : ranges_) {
        if (ctx.shouldYield()) {
            cb.emitDeferred(...);   // bail; resume next tick
            return;
        }
        // ... heavy work ...
    }
}
```

Every skip is reported on `engine.events<SystemSkipped>()` as a
`{tick, systemName, reason}` event. `reason` is `"budget"` for
budget-driven skips.

### Deterministic replay — `SkipPolicy::Scripted`

`Budget` mode is local to the machine — under wall-clock pressure
two peers can diverge. For networked / lockstep games:

```cpp
// Authoritative server:
engine.setSkipPolicy(SkipPolicy::Budget);
engine.setTickBudget(0.012);
// ... drain events<SystemSkipped>(), broadcast the log ...

// Clients replay:
engine.setSkipPolicy(SkipPolicy::Scripted);
for (const auto& [tick, name] : broadcastLog) {
    engine.pushScriptedSkip(tick, name);
}
```

`Scripted` ignores `tickBudget` entirely and consults the queue
verbatim. World hashes match the server's bit-for-bit when the same
skip log is applied.

## Per-job priorities — `JobPriority` (§3.5 batch 12)

`parallelFor` accepts an optional `JobPriority` (`High` / `Normal` /
`Low`). Higher-priority jobs are popped from a worker's own queue
before lower-priority ones, and cross-worker steals also scan in
priority order. Defaults to `Normal` everywhere, so adding the
argument never changes existing behavior.

```cpp
ctx.parallelFor(items.size(), 0,
    [](Range r, CommandBuffer& cb) { /* ... */ },
    JobPriority::High);   // jump the queue ahead of other waves' Normal work
```

Priorities are advisory — the engine's deterministic commit phase
runs after every wave fully settles, so changing priorities never
alters world state, only when work runs.

## How waves run

Within a wave the engine fans systems out across `workerCount - 1`
helper threads plus the simulation thread, and waits for all of them.
After every system in the wave has returned, the engine commits each
system's command buffers (in registration order) on the sim thread, then
advances to the next wave.

You get parallelism along two axes:

1. **System-level**: every non-conflicting system in a wave runs in
   parallel.
2. **Intra-system**: each system's `parallelFor` fans out across the same
   worker pool.

The two combine: many systems each producing many jobs all flow through
one shared work-stealing queue.

## Example: a two-system wave

The boids example pairs:

- `BoidsSystem` — reads `Transform, Velocity`, writes `Velocity`. Computes
  steering forces.
- `BoidsMoveSystem` — reads `Velocity, Transform`, writes `Transform`.
  Integrates velocity into position.

The two conflict on Velocity (one writes, the other reads) **and** on
Transform — so they land in distinct waves and run sequentially every
tick. If a third "debug logger" system declared `reads = {Transform}` and
`writes = {}`, it would share a wave with `BoidsSystem` (they only
overlap on `Transform`, which neither writes) but not with
`BoidsMoveSystem` (which writes `Transform`).

## Commit order vs execution order

This is the single most load-bearing rule:

> **Commit order is submission order, not execution order.**

`parallelFor` reserves one `CommandBuffer` per job *up front*; workers
race freely to fill them. After the wave completes, the engine walks each
system's command buffers in submission index order and commits them
strictly serially. So even though jobs run out of order, the world
mutations are applied in a fixed, reproducible sequence.

The commit phase is single-threaded on the sim thread, runs after all
jobs in the wave have returned, and never overlaps with any worker job
in the same wave.

## Determinism

With `Config::deterministic = true` the engine guarantees that the same
inputs produce the same world state across runs and machines. The
preconditions are:

- Systems are registered in the same order every run.
- System logic is itself deterministic (no untracked global state, no
  reads from the system clock from inside `parallelFor`, etc.).
- No user code spawns its own threads that mutate world state — and it
  shouldn't, since the commit phase is the only mutation path.

The commit-in-submission-order rule means the *execution* schedule of
jobs is allowed to vary; what's deterministic is the resulting world
state. `tests/determinism_test.cpp` pins this.

## Built-in systems

Today there is one:

- `makeHierarchySystem()` — propagates `Parent`-attached entities'
  world `Transform` from their parent's world `Transform` composed with
  `Parent::localOffset`. Reads `Transform, Parent`; writes `Transform`.
  Single-threaded inside `ctx.single()` so multi-level chains resolve in
  one pass. Register this **after** any movement systems that write
  `Transform` so it observes their commits in the next wave.

```cpp
engine.registerSystem(std::make_unique<MovementSystem>());
engine.registerSystem(threadmaxx::makeHierarchySystem());
```

See [Hierarchy](hierarchy.md) for the full story.

## Anti-patterns

- **Holding pointers/references to dense spans across ticks.** The spans
  are stable only during `update()`. A spawn or destroy between ticks
  can move every element.
- **Touching another entity's component from inside `parallelFor`.** You
  can *read* it (the spans are const), but you can't *write* it — and
  you can't even safely read commands you've queued in your own buffer.
  Two writes to the same entity in one wave will both apply, last-write-
  wins, in submission order.
- **Mutating from `onRegister` without going through the seed command
  buffer.** `onRegister` runs at engine setup, *before* `initialize()`
  returns, but the world is already live. Use `IGame::onSetup`'s
  `CommandBuffer& seed` for initial entities.
- **Sharing one `ISystem` instance between two engines.** A system's
  state is private to it, but the engine assumes exclusive ownership.

## Next

[Command buffers](command_buffers.md) covers the mutation API in detail.
