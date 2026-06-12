# threadmaxx_assets CHANGELOG

## v1.0.0 — 2026-06-12

Initial release. Asset import + cache sibling library for
`threadmaxx`-based games and tools. Shipped across eight batches
(A1–A8); per-batch detail in `FUTURE_WORK.md`.

### Highlights

- **Renderer-neutral PODs** — `MeshData`, `TextureData`,
  `AudioClipData`, `FontAtlas`. No GPU handles, no external decode
  library dependencies.
- **File-format importers** — OBJ (verts/normals/uv/face indices,
  vertex dedup, smoothed-normals fallback), PLY (ASCII + binary LE),
  BMP (24/32-bit), TGA (uncompressed + RLE), PNG (color types 2 / 6,
  8-bit, in-tree DEFLATE decoder), WAV (PCM s16 / float32, mono /
  stereo), BMFont (.fnt text + binary v3 + paired .png pages).
- **AssetRegistry** — canonical-path dedup, refcounted slots, RAII
  `AssetHandle<T>`, generation guard against stale handles, in-place
  reload, stats.
- **AsyncLoader** — worker pool with `pump()` drain, synchronous
  dedup via `findX`, opt-in `EngineAssetLoader` adapter that wires
  the pump into `Engine::step()` through the engine's
  `IResourceLoader` contract.
- **Cooked bundle** — `'TMAS'` magic + `u32` version, byte-identical
  serialize/deserialize round-trip, `BundleMount` RAII holder for
  mounted assets, `FilesystemWatcher` polling for hot reload.

### Performance

- OBJ cube parse (24 verts) < 50 µs.
- Async pump idle (no pending work): zero heap allocations.
- Registry handle copy: one relaxed atomic op.
- Crowd no-alloc gate (`test_assets_crowd_no_alloc`): 100 meshes /
  100 textures / 100 handle copies per "frame" over 100 measured
  frames, zero heap allocations under a tracking allocator.

### Test coverage

19 tests in `tests/assets/`, all green on `build/` and
`build-werror/` (`-Wsign-conversion -Wconversion -Wold-style-cast
-Werror`).

Categories: foundations (4) / mesh (3) / texture (4) / audio (1) /
font (1) / registry (1) / async (1) / engine bridge (1, gated) /
bundle (1) / watch (1) / crowd no-alloc gate.

### Public API

Every header under `include/threadmaxx_assets/` (except `detail/`)
is part of the v1.x ABI contract. SemVer bump rules + deprecation
policy are documented in `MAINTAINER_GUIDE.md`.

### Deferred to v1.x

- `.mtl` parser for OBJ.
- PNG paletted / 16-bit / interlaced.
- JPEG / EXR / KTX.
- OGG / Vorbis / MP3 / Opus.
- TrueType / OpenType rasterization (sibling to BMFont).
- Memory budget caps + LRU eviction.
- Streaming / chunked-load partial reads.
- Native `inotify` / `ReadDirectoryChangesW` watcher backends.
- glTF — wide-surface format, deserves its own batch series.
- USD / Alembic — DCC-side; out of scope.
