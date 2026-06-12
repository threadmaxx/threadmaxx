# `threadmaxx_assets` — User Guide

The library hands you `AssetRegistry`. Everything else flows through
it. This guide walks the pillars in the order you'll typically wire
them up.

## 1. Synchronous loads

```cpp
#include <threadmaxx_assets/threadmaxx_assets.hpp>
#include <threadmaxx_assets/loaders/obj.hpp>
#include <threadmaxx_assets/loaders/png.hpp>
#include <threadmaxx_assets/loaders/wav.hpp>
#include <threadmaxx_assets/loaders/bmfont.hpp>

using namespace threadmaxx::assets;

auto mesh = loadObj("assets/cube.obj");
if (!mesh.ok()) {
    std::cerr << "OBJ failed: " << mesh.message << "\n";
    return -1;
}
const auto& verts = mesh.value.vertices;
```

Every loader returns `AssetResult<T>{ value, code, message }`. `ok()`
is the success test; `code` is the `ErrorCode` enum
(`FileNotFound`, `BadMagic`, `UnsupportedFormat`, `Truncated`,
`ParseError`, `UnsupportedVersion`, …).

For tests and tools that already have the bytes in memory:

```cpp
auto bytes = readFile("input.obj");
auto m = parseObj(*bytes, "input.obj");
```

## 2. Registry + dedup

```cpp
#include <threadmaxx_assets/registry.hpp>

AssetRegistry reg;

auto a = reg.loadMesh("assets/cube.obj");
auto b = reg.loadMesh("assets/cube.obj");   // dedups: same id, refcount=2
auto c = reg.loadTexture("assets/icon.png");
```

`AssetHandle<T>` is a RAII refcount wrapper. Copy bumps; destroy
drops; on drop-to-zero the slot is freed and the generation is bumped
(stale handles read `nullptr`).

```cpp
if (a.valid()) {
    const auto& m = *a;          // operator* / operator-> sugar
    process(m.vertices, m.indices);
}
```

Inject pre-built data via `addX(name, value)`:

```cpp
MeshData proc = generateTriangle();
auto h = reg.addMesh("procedural/triangle", std::move(proc));
```

Force a re-read of the original source file:

```cpp
reg.reload(h.id());   // returns false on injected (non-file) slots
```

## 3. Async loading

```cpp
#include <threadmaxx_assets/async_loader.hpp>

AsyncLoader async(reg, /*workers=*/2);

auto h = async.enqueueMesh("assets/big.obj");   // returns immediately
// h.valid() is false until pump installs the result.

for (;;) {
    async.pump();
    auto ready = reg.findMesh("assets/big.obj");
    if (ready.valid()) break;
    std::this_thread::sleep_for(10ms);
}
```

`pump()` is zero-alloc on the empty fast path. Call it on whichever
thread owns asset installation (usually the sim thread).

**Subsequent enqueueX on the same path dedup synchronously** — they
return the existing handle without queuing new work.

## 4. Engine bridge

```cpp
#if THREADMAXX_ASSETS_HAS_ENGINE_BRIDGE
#include <threadmaxx_assets/engine_bridge.hpp>

engine.addResourceLoader(
    std::make_unique<EngineAssetLoader>(reg, async));
#endif
```

`EngineAssetLoader` is the `threadmaxx::IResourceLoader` adapter —
the engine will call `update()` (which calls `async.pump()`) once
per `step()`, on the sim thread, after `postStep` commits. `stats()`
surfaces pendingCount / inFlight / failed into the engine's HUD
aggregate.

## 5. Cooked bundles

```cpp
#include <threadmaxx_assets/bundle.hpp>

Bundle b;
b.meshes.emplace_back("hero",  std::move(mesh.value));
b.textures.emplace_back("ui",  std::move(tex.value));
auto bytes = writeBundle(b);
writeFile("game.tmas", *bytes);
```

Loading is one I/O + mount:

```cpp
auto loaded = readBundleFromFile("game.tmas");
auto mount  = mountBundleInto(reg, loaded.value);
// While `mount` lives, all bundle assets stay in the registry. They're
// queryable via reg.findMesh("hero"), reg.findTexture("ui"), etc.
```

Wire format: `[magic 'TMAS' u32][version u32][counts u32 ×4]` then per
asset `[nameLen u32][name][POD payload]`. Same caveat as
`WorldSnapshot`: host-endian POD, intended for shipping a build of
the same binary, not archival.

## 6. Hot reload

```cpp
#include <threadmaxx_assets/watch.hpp>

FilesystemWatcher w;
w.watch("assets/cube.obj");
w.watch("assets/icon.png");

// Each tick (typically once per frame):
for (const auto& path : w.tick()) {
    if (auto h = reg.findMesh(path);   h.valid()) reg.reload(h.id());
    if (auto h = reg.findTexture(path); h.valid()) reg.reload(h.id());
}
```

Poll-based; the first tick after `watch()` baselines the mtime so the
file isn't reported as "just changed".

## 7. Common gotchas

- **`AssetHandle<T>::get()` returns nullptr after refcount→0** even
  though `id()` still reads the old slot id. Always check `valid()`
  before dereferencing.
- **`mountBundleInto` return value is load-bearing.** Drop the
  `BundleMount` and every asset in the bundle drops to refcount 0
  and gets evicted (unless someone else took a reference via findX).
- **`AsyncLoader::pump()` MUST be called on the same thread that
  observes registry state** to avoid torn reads between install and
  the subsequent `findX`.
- **Reload on injected (non-file) slots fails.** `reload(id)` only
  works for slots installed via `loadX(path)`.
- **PNG paletted / 16-bit / interlaced** is not supported in v1.0 —
  re-export as RGB / RGBA 8-bit, non-interlaced.
- **WAV with `wFormatTag` other than 1 (PCM s16) or 3 (float32)**
  returns `UnsupportedFormat`. Re-encode in your DAW.
