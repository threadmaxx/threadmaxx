# Command Buffers

@page command_buffers Command Buffers

A `CommandBuffer` is a per-job, lock-free recorder of mutations. Worker
jobs emit commands into a buffer they exclusively own; the engine
collects every buffer after the wave completes and applies the commands
in submission order on the simulation thread.

## Available commands

```cpp
// Spawning.
void spawn(const Transform&, const Velocity& = {}, const RenderTag& = {},
           const UserData& = {}, const Acceleration& = {});
void spawn(const Transform&, const Velocity&, const RenderTag&,
           const UserData&, const Acceleration&,
           const Parent&, ComponentSet initialMask);

// Destruction.
void destroy(EntityHandle entity);

// Per-component setters. Some of these update the per-entity
// ComponentSet — see below.
void setTransform   (EntityHandle, const Transform&);
void setVelocity    (EntityHandle, const Velocity&);
void setRenderTag   (EntityHandle, const RenderTag&);   // also flips RenderTag bit
void setUserData    (EntityHandle, const UserData&);
void setAcceleration(EntityHandle, const Acceleration&);
void setParent      (EntityHandle, const Parent&);      // also flips Parent bit
void setComponentMask(EntityHandle, ComponentSet);      // raw mask override
```

`spawn` is the only command that produces an output handle. Today there
is no in-tick "tell me the handle I just spawned" API — the handle is
allocated during commit, after your job has returned. If a system needs
to set up entities that reference each other, run that work in a single
`ctx.single()` call where you can call `spawn` and capture the returned
handle inline. (This is a known limitation; future work item: stable
spawn handles available pre-commit.)

## Default component-mask derivation

The simple `spawn` overload derives the initial component mask:

- `Transform`, `Velocity`, `UserData`, `Acceleration`: always present.
- `RenderTag`: present iff `render.meshId >= 0`.
- `Parent`: absent (use the explicit overload to attach a parent at
  spawn).

The explicit-mask overload lets you set things any way you like:

```cpp
threadmaxx::Parent p{};
p.parent = parentEntity;
p.localOffset.position = {0, 1, 0};

ComponentSet mask;
mask |= Component::Transform;
mask |= Component::Velocity;
mask |= Component::Parent;
// RenderTag deliberately omitted

cb.spawn(t, v, /*render*/{}, /*userData*/{}, /*accel*/{}, p, mask);
```

## Setters that change presence

Two setters update the per-entity component mask:

- `setRenderTag` — sets the `RenderTag` bit iff the new `meshId >= 0`,
  clears it otherwise. So "make this entity not renderable" is simply
  `cb.setRenderTag(e, RenderTag{ .meshId = -1 })`.
- `setParent` — sets the `Parent` bit iff `p.parent.valid()`, clears it
  otherwise. To detach an entity: `cb.setParent(e, Parent{})`.

Every other setter writes the value without touching the mask.
`setComponentMask` is the escape hatch when none of those rules fit.

## How commit ordering works

```
   parallelFor count=N, grain=G
       │
       │ Engine reserves N/G CommandBuffer slots
       │ (jobs see stable pointers — important; see below)
       ▼
   workers race freely, each filling buffer[i]
       │
       │ latch waits for every job
       ▼
   sim thread commits buffer[0], buffer[1], ... in order
```

The reservation up front (a `resize`, not `emplace_back`) is what lets
pointers into the buffer vector stay valid while jobs run. Switching it
to push-on-submit would silently re-order commits under reallocation.

Within a wave that has multiple systems, each system has its own slot
list. The engine commits **system-by-system in registration order**, and
within each system, **buffer-by-buffer in submission order**. So if
System A is registered first and System B second, all of A's mutations
land before any of B's, even though their jobs ran interleaved.

## What happens to stale handles

Spawn and destroy bump the per-entity generation. A `setTransform` on an
entity that was destroyed earlier in the same commit phase is a no-op —
the engine silently drops the command (the generation in the handle no
longer matches the slot). This makes "destroy-then-set" sequences safe
even when emitted by different jobs.

It does *not* save you from logical races. If two jobs in the same wave
both `setTransform` the same entity, both apply and the later one (in
submission order) wins. The engine doesn't detect or warn about this.

## Sizing hints

If a system knows roughly how many commands per job, give the buffer a
head start:

```cpp
ctx.parallelFor(count, grain, [=](Range r, CommandBuffer& cb) {
    cb.reserve(r.size());
    for (auto i = r.begin; i < r.end; ++i) {
        // ... cb.setTransform(...) ...
    }
});
```

This is purely an allocation-amortization hint. Forgetting it doesn't
break anything; the buffer grows geometrically.

## Practical patterns

### Conditional destroy

```cpp
ctx.parallelFor(count, 0, [=](Range r, CommandBuffer& cb) {
    for (auto i = r.begin; i < r.end; ++i) {
        if (transforms[i].position.y < deathPlane) {
            cb.destroy(entities[i]);
        }
    }
});
```

### Spawn in a `single()` block

When you need to spawn a small fixed number of entities (and don't care
about parallelism for that work), use `single()`:

```cpp
ctx.single([this](Range, CommandBuffer& cb) {
    for (int i = 0; i < 3; ++i) {
        cb.spawn(Transform{}, Velocity{...}, RenderTag{1, 0});
    }
});
```

### Attaching a parent at spawn

```cpp
ctx.single([parent](Range, CommandBuffer& cb) {
    Parent attach{};
    attach.parent = parent;
    attach.localOffset.position = {0, 0.5f, 0};

    ComponentSet mask;
    mask |= Component::Transform;
    mask |= Component::Parent;

    cb.spawn(Transform{}, Velocity{}, RenderTag{1, 0}, UserData{},
             Acceleration{}, attach, mask);
});
```

## Next

[Hierarchy](hierarchy.md) covers `Parent` and the built-in hierarchy
system in depth.
