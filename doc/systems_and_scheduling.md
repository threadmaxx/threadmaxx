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
