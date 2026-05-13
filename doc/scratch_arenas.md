# Scratch arenas

@page scratch_arenas Scratch arenas

Per-job bump allocator for short-lived, POD-only scratch memory. Use it
for neighbor lists, sort buffers, prefix-sum scratch — anything you
build and throw away inside a job. The engine resets the arena at the
end of each wave so the storage is reused across ticks.

## The API

`ScratchArena` is paired with `CommandBuffer` on the new `parallelFor`
and `single` overloads:

```cpp
ctx.parallelFor(count, /*grain*/ 0,
    [&](threadmaxx::Range r,
        threadmaxx::CommandBuffer& cb,
        threadmaxx::ScratchArena& arena) {
        auto* tmp = arena.allocate<NeighborIndex>(r.size());
        // ... use tmp, no destructor needed ...
    });
```

The two-argument variant (`Range, CommandBuffer&`) is unchanged and
still works for jobs that don't need scratch.

## Allocation rules

- `arena.allocate<T>(count)` returns an uninitialized, properly-aligned
  pointer to `count` instances of `T`.
- `T` must be **trivially destructible** — a `static_assert` enforces
  this. The arena doesn't track destructors; reset just rewinds the
  bump pointer.
- The returned pointer is valid until the next `reset()` (the engine
  resets at wave end) or the arena's destruction, whichever comes
  first.
- Growing the arena pushes a new chained slab; previously-issued
  pointers stay valid until the next reset.

## Lifetime

Each `parallelFor` chunk and each `single` call gets its own arena,
stored alongside its `CommandBuffer` in the per-system context. When
the wave ends, the contexts are destroyed and the arenas' slabs are
returned to `std::vector`'s `unique_ptr` cleanup. For a steady-state
system the slabs are large enough on the first tick that subsequent
ticks pay zero allocation cost — they just reset the bump pointer.

## When NOT to use it

- For state that must live past the wave — use a member of your
  `ISystem`, or write back into ECS via the `CommandBuffer`.
- For types with destructors or RAII (file handles, mutexes,
  `std::vector`s themselves) — the arena will not call them. Use a
  regular `std::vector` instead.
- For very small allocations where the bookkeeping savings don't
  matter; `std::vector` with a reserve is fine.

## Worked example: neighbor query

```cpp
void update(threadmaxx::SystemContext& ctx) override {
    ctx.parallelFor(world.size(), /*grain*/ 0,
        [&](threadmaxx::Range r,
            threadmaxx::CommandBuffer& cb,
            threadmaxx::ScratchArena& arena) {
            // Scratch buffer sized for this chunk.
            auto* nearby = arena.allocate<std::uint32_t>(r.size() * kMaxNeighbors);
            std::uint32_t* writer = nearby;
            for (std::uint32_t i = r.begin; i < r.end; ++i) {
                writer = collectNeighbors(world, i, writer, kMaxNeighbors);
            }
            // ... use the dense neighbor list ...
        });
}
```

A `std::vector` here would allocate once per chunk per tick, freeing at
end of job. The arena reuses the same backing buffer across ticks.
