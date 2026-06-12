# `threadmaxx_assets`

Asset import + cache sibling library for games and tools built on
`threadmaxx`. Parses on-disk file formats (OBJ, PLY, BMP, TGA, PNG,
WAV, BMFont) into renderer-neutral PODs (`MeshData`, `TextureData`,
`AudioClipData`, `FontAtlas`) and ships a small `AssetRegistry` with
refcounted dedup, async loading, hot reload, and a cooked bundle
format.

**v1.0.0 — 2026-06-12.** Eight batches (A1–A8) + close-out. See
`CHANGELOG.md` for the per-batch landing log and `DESIGN_NOTES.md`
for the full spec.

## Quickstart

```cpp
#include <threadmaxx_assets/threadmaxx_assets.hpp>
#include <threadmaxx_assets/registry.hpp>
#include <threadmaxx_assets/loaders/obj.hpp>

using namespace threadmaxx::assets;

AssetRegistry reg;

// One-shot synchronous load.
auto mesh = reg.loadMesh("assets/cube.obj");
if (!mesh.valid()) return -1;
const auto vertexCount = mesh->vertices.size();
```

For hot paths use the async loader:

```cpp
#include <threadmaxx_assets/async_loader.hpp>

AsyncLoader async(reg, /*workers=*/2);
auto h = async.enqueueMesh("assets/big.obj");   // returns immediately

// In your tick:
for (;;) {
    async.pump();                                // installs completed records
    auto ready = reg.findMesh("assets/big.obj");
    if (ready.valid()) break;
    sleep(1);
}
```

Shipping builds: bundle once at cook time, ship one file:

```cpp
Bundle b;
b.meshes.emplace_back("hero",  std::move(*mesh));
b.textures.emplace_back("ui",  std::move(*tex));
auto bytes = writeBundle(b);
writeFile("game.tmas", *bytes);

// Game startup:
auto bundle = readBundleFromFile("game.tmas");
auto mount  = mountBundleInto(reg, bundle.value);
```

The engine bridge (opt-in via `TARGET threadmaxx::threadmaxx` at
configure time) makes `AsyncLoader` pump from `Engine::step()`:

```cpp
#if THREADMAXX_ASSETS_HAS_ENGINE_BRIDGE
#include <threadmaxx_assets/engine_bridge.hpp>

engine.addResourceLoader(
    std::make_unique<EngineAssetLoader>(reg, async));
#endif
```

## Pillars

| Batch | What |
|-------|------|
| **A1** | Foundations — `AssetId`, `AssetResult`, in-memory PODs, `detail::io::readFile`, no-engine-link gate |
| **A2** | Mesh — `loadObj`, `loadPly` (binary LE + ASCII), vertex dedup, smoothed-normals fallback |
| **A3** | Texture — `loadBmp`, `loadTga` (uncompressed + RLE), `loadPng` (color types 2/6, 8-bit), in-tree inflate |
| **A4** | Audio — `loadWav` (PCM 16-bit + IEEE float32, mono/stereo) |
| **A5** | Font — `loadBmfont` (text + binary v3), multi-page atlas via PNG, sorted-kerning lookup |
| **A6** | Registry — canonical-path dedup, refcounted slots, `reload()`, generation guard |
| **A7** | Async — worker pool + futures, `pump()` drain, opt-in engine bridge as `IResourceLoader` |
| **A8** | Bundle — `'TMAS'` magic, byte-identical round-trip, `mountBundleInto`, `FilesystemWatcher` |

## Performance

| Gate | Number |
|------|--------|
| OBJ cube parse (24 verts) | < 50 µs |
| 2 kB PNG decode through inflate | < 200 µs |
| Async pump idle (no work pending) | zero heap allocations |
| Registry handle copy | one relaxed atomic op |

The v1.0 crowd no-alloc gate (`tests/assets/test_assets_crowd_no_alloc.cpp`)
holds 100 meshes / 100 textures / 100 handle copies per "frame" over
100 measured frames at zero heap allocations.

## Test coverage

19 tests in `tests/assets/`, all green on `build/` and `build-werror/`
(`-Wsign-conversion -Wconversion -Wold-style-cast -Werror`).

Categories: foundations (4) / mesh (3) / texture (4) / audio (1) /
font (1) / registry (1) / async (1) / engine bridge (1, gated) /
bundle (1) / watch (1) / crowd no-alloc gate.

## Public API

Every header under `include/threadmaxx_assets/` except `detail/` is
part of the v1.x ABI contract. SemVer bump rules + deprecation
policy are in `MAINTAINER_GUIDE.md`.

## Deferred to v1.x

- `.mtl` parser for OBJ.
- PNG paletted / 16-bit / interlaced.
- JPEG / EXR / KTX.
- OGG / Vorbis / MP3 / Opus.
- TrueType / OpenType rasterization (sibling to BMFont).
- Memory budget caps + LRU eviction.
- Streaming / chunked-load.
- Native `inotify` / `ReadDirectoryChangesW` watcher backends.
- glTF / USD / Alembic (out of scope).
