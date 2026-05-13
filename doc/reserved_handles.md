# Reserved spawn handles

@page reserved_handles Reserved spawn handles

Normally `CommandBuffer::spawn(...)` only knows the assigned
`EntityHandle` after the commit phase runs — that's a problem when a
single job needs to spawn a parent and a child together. Reserved
handles fix that: take the handle up front, then materialize it during
commit.

```cpp
void update(threadmaxx::SystemContext& ctx) override {
    threadmaxx::EntityHandle parent = ctx.reserveHandle();
    threadmaxx::EntityHandle child  = ctx.reserveHandle();

    ctx.single([parent, child](threadmaxx::Range,
                                threadmaxx::CommandBuffer& cb) {
        cb.spawn(parent, parentTransform);
        cb.spawn(child,  childTransform, {}, {}, {}, {},
                 threadmaxx::Parent{parent, localOffset},
                 maskWithParent());
    });
}
```

Outside a system body — during `IGame::onSetup` — use
`Engine::reserveEntityHandle()`:

```cpp
void onSetup(Engine& eng, World&, CommandBuffer& seed) override {
    auto root = eng.reserveEntityHandle();
    auto leaf = eng.reserveEntityHandle();
    seed.spawn(root, Transform{Vec3{0,0,0}, {}, {1,1,1}});
    seed.spawn(leaf, Transform{}, {}, {}, {}, {},
               Parent{root, Transform{Vec3{0,2,0}, {}, {1,1,1}}},
               maskWithParent());
}
```

## Lifecycle

```
worker (or onSetup): ctx.reserveHandle()
       slot allocated, generation bumped, reserved=true
       handle returned to caller (handle.valid() == true,
                                  world.tryGetTransform(handle) == nullptr)

worker:              cb.spawn(handle, ...)
       command recorded, will run during commit

commit (sim thread): materializeReserved(handle, ...)
       reserved=false, alive=true, dense arrays grow
       handle is now the standard live entity handle

step end (sim thread): discardAllReservations()
       any reservation not consumed by a cb.spawn(handle, ...)
       is reaped: generation bumped, slot returned to free list
```

Important consequences:

- A reserved handle reads as `world.alive(handle) == false` until
  materialized — workers that follow another worker's reservation
  must use it in a `cb.spawn(handle, ...)` rather than
  `world.tryGetTransform(handle)`.
- An unconsumed reservation is reaped at step end. Workers that bail
  before recording the matching spawn don't leak slots.
- The reservation generation is unique. A reaped handle never aliases
  a future entity.

## Thread safety

`ctx.reserveHandle()` (and `Engine::reserveEntityHandle()`) is
internally synchronized — safe to call from any worker job. Reservation
under contention is fast (a single mutex around a free-list pop +
generation bump), but it's not free; if you find yourself reserving in
a tight loop, consider batching the work into a `single()` block.

## Alternatives

- If you just want the handle *eventually* and can defer the
  parent-child link by one tick, the simpler pattern is to spawn the
  parent now, capture its handle from outside (read the dense
  `entities` span), and spawn the child next tick with a `setParent`
  command. Reserved handles are the right tool when you need both
  spawned in the same recording.
