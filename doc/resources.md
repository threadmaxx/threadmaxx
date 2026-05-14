# Resources

@page resources Resources

`ResourceRegistry` is the engine's typed, thread-safe store for things the
engine doesn't natively own — meshes, textures, audio clips, animation
clips, anything game-side. The library does not load anything for you;
it just gives you a stable typed handle and a thread-safe lookup.

## The handle

```cpp
template <typename T>
struct ResourceId {
    std::uint32_t index;
    std::uint32_t generation;

    constexpr bool valid() const noexcept;          // true iff generation != 0
    constexpr bool operator==(const ResourceId&) const noexcept = default;
};
```

The type tag is purely compile-time. At runtime the registry stores
resources type-erased and validates the type on every call via
`std::type_index`, so passing a `ResourceId<Mesh>` to
`get<Texture>(...)` returns `nullptr` even if the index happens to be in
range — the type check fails first.

`index/generation` mirrors `EntityHandle`: removing a slot bumps its
generation, so stale handles never alias new ones.

## The registry

The engine owns one `ResourceRegistry`. Get it from `Engine::resources()`:

```cpp
auto& reg = engine.resources();

threadmaxx::ResourceId<Mesh> oakId = reg.add(loadMesh("oak.gltf"));
threadmaxx::ResourceId<Mesh> pineId = reg.add(loadMesh("pine.gltf"));

if (const Mesh* m = reg.get(oakId)) {
    // ... draw with m ...
}

reg.remove(oakId);
// reg.get(oakId) now returns nullptr.
// pineId is still valid.
```

The registry has a single internal mutex. Reads and writes are safe from
any thread, including worker jobs. The mutex is fine for setup-time
registration and per-frame lookups; it is **not** designed for "I load
1000 resources concurrently". For an async loader, do the I/O off-thread
and call `add()` once each resource is ready.

## Refcounted handles (`ResourceHandle<T>`)

The legacy `add` / `remove` pair gives you a bare `ResourceId<T>` and
trusts the caller to drop the slot manually. The §3.2 batch-7
`addRefCounted` path returns an RAII handle that owns its slot: copies
bump the refcount, destruction decrements, and the last drop frees the
slot AND bumps generation so stale ids never alias.

```cpp
auto& reg = engine.resources();

ResourceHandle<Mesh> primary = reg.addRefCounted(loadMesh("oak.gltf"));
const ResourceId<Mesh> id = primary.id();

// Share ownership: bump the refcount.
ResourceHandle<Mesh> shared = reg.acquire(id);
assert(reg.refCount(id) == 2);

primary.reset();              // refcount → 1, slot still alive
// when `shared` goes out of scope here, slot is freed automatically
```

Key properties:

- **Copy bumps**, **move transfers**, **default-constructed is null**.
- `acquire(id)` returns a null handle for non-refcounted slots and stale
  ids. The two paths (`add`+`remove` vs. `addRefCounted`+handle) don't
  mix on the same slot.
- `ResourceHandle<T>::id()` exposes the bare id for interop with
  renderer code that only takes a `ResourceId<T>`. The slot stays alive
  as long as at least one handle copy survives, so it's safe to hold
  the bare id transitively while a handle is in scope.
- `reset()` detaches eagerly. `~ResourceHandle()` does the same.

Hot-reload integration (§3.2 batch 7) emits `AssetReloaded{oldId, newId}`
when a loader replaces an asset — subscribe to the event channel and
rewire any cached `ResourceId<T>` your game code holds. Refcounted
handles do NOT auto-redirect across reloads; that would require a
trampoline layer the library deliberately doesn't ship. Game code
controls the rewire timing.

## Lifetime model

The registry owns each stored value via `std::shared_ptr<void>` with a
type-aware deleter. `remove()` drops the registry's reference; the value
is destroyed when the last outstanding `shared_ptr` is gone. With the
refcounted handle path, the last handle destruction is what triggers
release — `remove()` is not called explicitly.

The registry lives as long as the `Engine` does. It never reseats and is
never replaced.

## What goes in here

Anything game code wants to refer to by a stable handle that:

- is constructed once (or rarely),
- is shared between many entities or systems,
- needs to be looked up from worker jobs.

Typical examples:

- `Mesh`, `Material`, `Texture` — the renderer-side asset bundle for a
  given `RenderTag::meshId`. (The engine's `RenderTag::meshId` is just
  an `int32_t` — your renderer maps it onto a `ResourceId<Mesh>`
  internally.)
- `AnimationClip`, `Skeleton` — when you add a skinning system.
- `AudioClip`.
- Pre-baked navigation graphs.

What doesn't belong:

- Per-entity state (use a component).
- Things that change every tick (use a component or world-scoped data).
- One-shots that the engine should clean up automatically — there's no
  automatic GC for the bare `add` path; you call `remove()`. Use
  `addRefCounted` if you want automatic cleanup.

## Worked example: hooking a mesh registry to a renderer

```cpp
// Game side.
struct Mesh { /* vertex buffers, index buffers, etc. */ };

void setupMeshes(threadmaxx::Engine& engine) {
    auto& reg = engine.resources();
    oakMesh_  = reg.add(loadMesh("assets/oak.gltf"));
    pineMesh_ = reg.add(loadMesh("assets/pine.gltf"));
}

// Spawning code uses RenderTag::meshId as a *small int* keyed to the
// renderer's own mapping table; the engine doesn't interpret it.
seed.spawn(t, v, threadmaxx::RenderTag{ .meshId = 0 });
seed.spawn(t, v, threadmaxx::RenderTag{ .meshId = 1 });

// Renderer side, per frame.
void MyRenderer::submitFrame(const threadmaxx::RenderFrame& frame) {
    const auto& reg = engine_.resources();
    for (const auto& inst : frame.instances) {
        threadmaxx::ResourceId<Mesh> handle = meshIdMap_[inst.meshId];
        if (const Mesh* m = reg.get(handle)) {
            draw(*m, inst.transform);
        }
    }
}
```

The mapping from `RenderTag::meshId` to `ResourceId<Mesh>` is your
renderer's concern; the engine deliberately doesn't dictate it so two
games can use the same engine with different asset pipelines.

## Concurrency

```cpp
// Safe from a worker job:
ctx.parallelFor(N, 0, [&reg, &handles](Range r, CommandBuffer&) {
    for (auto i = r.begin; i < r.end; ++i) {
        if (const Mesh* m = reg.get(handles[i])) {
            // ... read-only use ...
        }
    }
});
```

`get`, `add`, `remove`, `count`, `addRefCounted`, `acquire`, and
`refCount` are all thread-safe. The pointer returned by `get` remains
valid as long as you hold its `ResourceId` *and* nothing calls `remove`
on it (or, for refcounted slots, the refcount stays above zero). In
practice that's "for the duration of the job that fetched it" — don't
stash raw `const T*` pointers across ticks unless you can guarantee
nobody removes the resource.

If you need long-lived references that outlive `remove()`, store a copy
of the value (or your own `shared_ptr` to it) outside the registry. The
registry's contract is "you give me a value, I give you a key" — not "I
am a long-term cache".
