# `threadmaxx_assets` — Maintainer Guide

How to extend `threadmaxx_assets` without breaking the v1.x contract.

## Versioning

SemVer. `version.hpp` carries `THREADMAXX_ASSETS_VERSION` and
`version_string()`. The `MAJOR * 10000 + MINOR * 100 + PATCH` integer
form is the hard pin — bump it together with the literal each release.

- **Patch** — bug fixes that preserve every observable behavior. Tests
  pass without changes.
- **Minor** — additive surface (new headers, new loaders, new
  `PixelFormat`/`SampleFormat` alternatives at the end of the enum,
  new bundle record types). Existing call sites keep compiling and
  produce identical results.
- **Major** — breaking changes (renames, removals, on-disk format
  bumps for `Bundle`, behavior changes in existing function signatures).
  Avoid; deprecate first.

## ABI

Every header under `include/threadmaxx_assets/` except `detail/` is
part of the v1.x ABI contract.

Most sensitive types:
- `MeshVertex` — `sizeof == 32` is a static_assert. Don't change.
- `MeshData`, `TextureData`, `AudioClipData`, `FontAtlas` — adding a
  field to any of these is **NOT** purely additive: the bundle format
  serializes them, so a field bump shifts the on-disk record offsets.
  Bump `kBundleVersion` AND keep a v1 loader path alive.
- `AssetHandle<T>` — three fields (`registry`, `id`, `generation`).
  Don't add data; the hot path is a single atomic op.
- `AssetRegistry::Impl` is technically reachable through
  `registry.hpp` (made non-private so the Registry.cpp templated
  helpers can name it), but it is **NOT** part of the ABI contract.
  Anything in `Impl` can change without a bump.

`detail/` is private. Rearrange freely.

## Hot-path discipline

The library's contract includes `zero allocations after warmup` for:
- `AssetRegistry::tryGet` / `findX` / `refCount` / `pathOf`,
- `AssetHandle<T>` copy / move / destroy,
- `AsyncLoader::pump()` on the empty-queue fast path.

When adding code that runs on the hot path:

- **Don't introduce `std::string` allocation** in `findX` /
  `tryGet`. The canonical key is computed once.
- **Don't iterate `std::unordered_map<…, AssetSlot>`** — slots live in
  a vector indexed by AssetId.
- **Don't call `addX` from inside a `parallelFor` body** — `addX`
  takes the write lock and serializes the world. Queue, drain in
  `pump()` or `update()` instead.
- **Pin a no-alloc gate** for any new API that runs per-frame.
  Mirror `test_assets_crowd_no_alloc`.

## Adding a new file format

1. Add the loader header under `include/threadmaxx_assets/loaders/`.
   Public signature: `AssetResult<X> loadFoo(std::string_view path)`
   plus `parseFoo(span<const std::byte>, std::string_view sourcePath = {})`.
2. Implement under `src/threadmaxx_assets/loaders/Foo.cpp`. Use
   `detail::io::readFile` to read bytes, then call your in-memory
   parser. Return `ErrorCode::Truncated` / `BadMagic` /
   `UnsupportedFormat` / `ParseError` as appropriate.
3. Add the loader to the registry's extension-dispatch in
   `AssetRegistry::loadMesh` / `loadTexture` / `loadAudio` /
   `loadFont` (and the same arm in `reload()`).
4. Mirror in `AsyncLoader::enqueueMesh` / etc. dispatch.
5. Add at least one pinned-bytes test under `tests/assets/`. Build
   the smallest valid fixture in the test source (synthetic bytes),
   not on disk, unless the fixture is big or shared.
6. List the format in `README.md`'s pillar table.

## Adding a new in-memory POD type

Touches every layer. Cross all of:

1. Define the POD in `include/threadmaxx_assets/data/`.
2. Add an `AssetType` enum bit — **append at the end**, don't reorder.
3. Add `AssetRegistry::loadX`, `addX`, `findX`. The templated helpers
   in `Registry.cpp` can be reused; add a `tryGet<T>` explicit
   instantiation at the bottom of `Registry.cpp`.
4. Add to `AsyncLoader::enqueueX`, the `WorkType` enum, the
   `DoneRecord` payload union, and the pump dispatch.
5. Add a `Bundle` field, write/read helpers in `Bundle.cpp`, the
   header / footer counts in `writeBundle` / `readBundle`. Bump
   `kBundleVersion` (this is a minor bump but on-disk wire change).
6. Mirror in `BundleMount` (`include/threadmaxx_assets/bundle.hpp`).
7. Mirror in `mountBundleInto`.
8. Add a test.

## On-disk formats

Both `Bundle` and the bundle records use a tagged header
(`'TMAS'` + `u32` version). Bump the version field when you change
the layout AND keep the v1 loader compatible — write `if (version
== 1) ... else if (version == 2)`, don't just reject old files.

Hosts that persist bundles across builds (replay archives, shipping
content paks) need migration paths — surface that in CHANGELOG.

## Things NOT to do

- Don't link `<libpng>`, `<libjpeg>`, `<stb_image>`, or any external
  decode library. The whole point of this library is no external
  dependencies. PNG decode rides on the in-tree `detail/Inflate.cpp`;
  add more formats the same way.
- Don't add `std::filesystem` to the public surface. The loader
  signatures take `std::string_view` so callers aren't forced into
  `std::filesystem::path`.
- Don't bypass the registry's write lock from a worker. `addX` is
  the only safe install path; the AsyncLoader pump runs on the
  calling thread.
- Don't reintroduce a "load by typeid" API. The four typed loaders
  (mesh / texture / audio / font) are the contract; adding a fifth
  type adds one method, not a template.
- Don't ship a real `inotify` / `kqueue` / `ReadDirectoryChangesW`
  watcher without a portability test matrix. Poll-based works on
  every platform; the native paths are nice-to-have.
