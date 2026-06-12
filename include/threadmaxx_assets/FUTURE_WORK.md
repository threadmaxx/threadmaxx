# `threadmaxx_assets` — future work

Status: **v1.0.0 shipped (2026-06-12)**. Eight batches (A1–A8) +
close-out, every gate green on `build/` and `build-werror/`. This
file remains as the historical scoreboard; the live spec is
`DESIGN_NOTES.md` and the running release log is `CHANGELOG.md`.

The library's spec lives in `DESIGN_NOTES.md`; this file is the
schedule + scoreboard.

## v1.0 batch series

Eight batches (`A1`…`A8`) plus a v1.0 close-out. Each batch is one
shippable PR — gated on `ctest --output-on-failure` green, `-Werror`
clean, and a per-batch test that pins the contract.

### A1 — Foundations

**Goal**: lay the no-engine-link static lib + the in-memory POD shapes
+ a single I/O entry. Subsequent batches build on top.

Deliverables:
- `include/threadmaxx_assets/version.hpp` (`THREADMAXX_ASSETS_VERSION
  = 100`, `version_string() = "0.1.0"`).
- `include/threadmaxx_assets/config.hpp` — capacity caps
  (`kInitialMeshVertexCapacity` etc.), policy knobs.
- `include/threadmaxx_assets/types.hpp` — `AssetId`, `AssetType`,
  `ErrorCode`, `AssetResult<T>`.
- `include/threadmaxx_assets/data/*.hpp` — `MeshData`, `TextureData`,
  `AudioClipData`, `FontAtlas` + nested POD types.
- `include/threadmaxx_assets/detail/io.hpp` + `Io.cpp` — `readFile`
  and `readFileInto`.
- `include/threadmaxx_assets/threadmaxx_assets.hpp` — umbrella.
- `src/threadmaxx_assets/CMakeLists.txt` — static target, alias
  `threadmaxx::assets`, the same warnings-as-errors gate as the input
  library.
- `tests/assets/test_assets_no_engine_link.cpp` — `#error` canary
  that fires if any `threadmaxx/` core header sneaks in.
- `tests/assets/test_assets_version.cpp` — `version_string()` literal
  pin.
- `tests/assets/test_assets_pod_layout.cpp` — `is_trivially_*` /
  `sizeof` checks on `MeshVertex`, `TextureData`'s header part, etc.

Test gate: 3 passing tests, lib compiles with -Werror.

Status: ✅ landed 2026-06-12.

### A2 — Mesh importers

**Goal**: `loadObj` + `loadPly`, vertex dedup, smoothed-normals
fallback, triangulation.

Deliverables:
- `include/threadmaxx_assets/loaders/obj.hpp` + `Obj.cpp`.
- `include/threadmaxx_assets/loaders/ply.hpp` + `Ply.cpp`.
- A bundled `cube.obj` test fixture under `tests/assets/fixtures/`.
- A bundled binary-LE PLY test fixture (smallest possible).
- `test_assets_obj_cube_golden.cpp` — parse cube.obj, assert
  vertex/index arrays byte-for-byte against a recorded baseline.
- `test_assets_obj_smoothed_normals.cpp` — parse a `vn`-less OBJ,
  assert normals are unit-length and average toward the face
  bisectors.
- `test_assets_ply_binary.cpp` — parse a 6-vertex / 2-face binary-LE
  PLY, assert positions and indices.
- `test_assets_obj_parse_in_memory.cpp` — `parseObj(span)` round-trip.

Status: ✅ landed 2026-06-12.

### A3 — Texture importers

**Goal**: `loadBmp` + `loadTga` + `loadPng`, top-left origin output.

Deliverables:
- `include/threadmaxx_assets/loaders/bmp.hpp` + `Bmp.cpp`.
- `include/threadmaxx_assets/loaders/tga.hpp` + `Tga.cpp`.
- `include/threadmaxx_assets/loaders/png.hpp` + `Png.cpp`.
- `include/threadmaxx_assets/detail/inflate.hpp` + `Inflate.cpp` —
  RFC1951 DEFLATE decoder.
- Tiny synthetic fixtures (4×4 checker, 8×8 gradient) under
  `tests/assets/fixtures/`.
- `test_assets_bmp_24bit.cpp` / `test_assets_bmp_32bit.cpp`.
- `test_assets_tga_uncompressed.cpp` / `test_assets_tga_rle.cpp`.
- `test_assets_png_rgb.cpp` / `test_assets_png_rgba.cpp`.
- `test_assets_inflate_unit.cpp` — pumps DEFLATE-encoded bytes through
  the decoder independently of PNG framing.

Status: ✅ landed 2026-06-12.

### A4 — Audio importers

**Goal**: `loadWav`, PCM 16-bit + float32, mono/stereo.

Deliverables:
- `include/threadmaxx_assets/loaders/wav.hpp` + `Wav.cpp`.
- Fixture: 1 kHz square @ 44.1 kHz, 1024-sample mono, PCM 16.
- Fixture: 440 Hz sine @ 48 kHz, 512-sample stereo, float32.
- `test_assets_wav_pcm16.cpp`, `test_assets_wav_float32.cpp`.
- `test_assets_wav_durations.cpp` — `sampleFrames` / `durationSeconds`
  match the fixture metadata.
- `test_assets_wav_unsupported_format.cpp` — IMA ADPCM cookie returns
  `UnsupportedFormat`.

Status: ✅ landed 2026-06-12.

### A5 — Font importers

**Goal**: `loadBmfont` text + binary, multi-page atlas via `loadPng`.

Deliverables:
- `include/threadmaxx_assets/loaders/bmfont.hpp` + `Bmfont.cpp`.
- Fixture: smallest valid BMFont text `.fnt` + matching PNG page.
- Fixture: same content, BMFont v3 binary format.
- `test_assets_bmfont_text.cpp`, `test_assets_bmfont_binary.cpp`.
- `test_assets_bmfont_kerning_lookup.cpp` — binary-search glyph
  lookup contract.

Status: ✅ landed 2026-06-12.

### A6 — Registry + dedup

**Goal**: canonical-path dedup, refcounted slots, reload contract.

Deliverables:
- `include/threadmaxx_assets/registry.hpp` + `Registry.cpp`.
- `test_assets_registry_dedup.cpp` — two `loadMesh("cube.obj")`
  calls return handles to the same slot id, refcount 2.
- `test_assets_registry_refcount_release.cpp` — handles drop to zero
  → slot freed + generation bumped.
- `test_assets_registry_reload_swap.cpp` — `reload(id)` swaps content
  in place; existing handles see new pointer values; refcount
  preserved.
- `test_assets_registry_stats.cpp` — `Stats` counters.
- `test_assets_handle_pod.cpp` — `AssetHandle<T>` is trivially
  copyable in terms of the slot pointer (move + atomic increment).

Status: ✅ landed 2026-06-12.

### A7 — Async loader + engine bridge

**Goal**: worker pool + futures, `pump()` drain on caller thread.
Opt-in `EngineAssetLoader` when `threadmaxx::threadmaxx` is linked.

Deliverables:
- `include/threadmaxx_assets/async_loader.hpp` + `AsyncLoader.cpp`.
- `include/threadmaxx_assets/engine_bridge.hpp` + `EngineBridge.cpp`
  (gated by `TARGET threadmaxx::threadmaxx`).
- `test_assets_async_enqueue.cpp` — enqueue mesh, pump until
  handle.valid(), pointer matches sync `loadMesh`.
- `test_assets_async_pump_idle_no_alloc.cpp` — empty pump path is
  zero-alloc.
- `test_assets_engine_bridge_loader.cpp` (only built when the engine
  is linked) — register `EngineAssetLoader`, step engine, assert
  pump fires.

Status: ✅ landed 2026-06-12.

### A8 — Cooked bundle + hot reload

**Goal**: `'TMAS'` bundle format, byte-identical round-trip,
filesystem-watch polling.

Deliverables:
- `include/threadmaxx_assets/bundle.hpp` + `Bundle.cpp`.
- `include/threadmaxx_assets/watch.hpp` + `Watch.cpp`.
- `test_assets_bundle_round_trip.cpp` — serialize → deserialize →
  serialize is byte-identical for a `Bundle` containing one of each
  asset type.
- `test_assets_bundle_header.cpp` — bad magic / bad version return
  `BadMagic` / `UnsupportedVersion`.
- `test_assets_bundle_mount.cpp` — `mountBundleInto(reg, b)` makes
  every blob loadable through `reg.loadMesh(name)` etc.
- `test_assets_watch_poll.cpp` — touch a file → next `tick()` reports
  it. Untouched files are silent.

Status: ✅ landed 2026-06-12.

### v1.0 close-out

**Goal**: bump version, ship docs, pin the crowd no-alloc gate,
ship a demo.

Deliverables:
- `include/threadmaxx_assets/version.hpp` bumped to `1.0.0`
  (`THREADMAXX_ASSETS_VERSION = 10000`, `version_string() = "1.0.0"`).
- `include/threadmaxx_assets/README.md` — production-ready
  quickstart + perf table.
- `include/threadmaxx_assets/USER_GUIDE.md` — pillar-by-pillar
  walkthrough (loaders → registry → async → bundle → watcher).
- `include/threadmaxx_assets/MAINTAINER_GUIDE.md` — SemVer rules,
  ABI contract, hot-path discipline.
- `include/threadmaxx_assets/CHANGELOG.md` — v1.0.0 entry.
- `tests/assets/test_assets_crowd_no_alloc.cpp` — 1000 meshes /
  1000 textures / 100 handle copies per "frame" over 100 measured
  frames after warmup, zero heap traffic under a tracking allocator.
- `examples/assets_demo/main.cpp` — headless: loads OBJ + PNG + WAV
  + BMFont, dedups a re-load, writes a `Bundle`, reads it back
  byte-identical, exits 0.

Status: ✅ landed 2026-06-12.

## Deferred (out of v1.0)

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

All of these are additive — minor bumps in the v1.x series once the
engine / editor pulls them in.
