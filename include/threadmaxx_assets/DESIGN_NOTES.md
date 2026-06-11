# `threadmaxx_assets` — asset import + cache sibling library

## 1. Purpose

`threadmaxx_assets` is the on-disk → in-memory POD pipeline for games and
tools built on `threadmaxx`. It owns the parsers that turn third-party
file formats (OBJ, PLY, BMP, TGA, PNG, WAV, BMFont) into renderer-neutral
structs the engine, audio, and UI siblings already speak, and a small
registry that dedups, hot-reloads, and bridges into `threadmaxx`'s
`ResourceRegistry` when the engine is linked.

It is for:

- parsing common mesh, texture, audio, and font formats into POD
  structs (`MeshData`, `TextureData`, `AudioClipData`, `FontAtlas`),
- a synchronous `load*` API for one-shot tooling work,
- an async loader pool for hot paths (preload, streaming-in-the-large),
- canonical-path-keyed dedup so the same OBJ loaded twice produces one
  owning record + two handles,
- a compact cooked-bundle format (`'TMAS'` magic) for shipping builds
  that bypass the third-party parsers entirely,
- hot-reload notifications driven by a polling filesystem watcher,
- an opt-in bridge that registers the loader as a
  `threadmaxx::IResourceLoader` so the engine can pump it once per tick.

It is **not** for:

- GPU upload — the renderer takes `TextureData::pixels` / `MeshData::vertices`
  and uploads on its own clock,
- audio playback — the audio library accepts `AudioClipData` and decides
  when to mix,
- streaming / virtual textures / megatexture / mip residency — out of
  scope; the cooked-bundle format is friendly to a future streamer but
  v1.0 ships only whole-file loads,
- 3D math beyond axis-aligned bounding boxes (the engine /
  `threadmaxx_simd` own transform pipelines),
- save game / world snapshots — the engine's `WorldSnapshot` covers
  that,
- animation rigs / skeletons (`threadmaxx_animation`'s territory),
- shader bytecode loading — the renderer host embeds SPIR-V; the
  engine deliberately doesn't generalize that.

That puts the cut where every engine / editor I've reviewed draws it
naturally: **above raw file I/O, below renderer / audio upload**. The
renderer, the audio mixer, the UI font cache, and any in-game pickup-
spawn helper all consume the same `MeshData` / `TextureData` /
`AudioClipData` PODs.

## 2. Design principles

1. **Renderer-neutral PODs.** Every output struct is a trivially-
   copyable POD shape with owning `std::vector` payloads. No GPU
   handles, no `vk::Image`, no shader bytecode — just data the caller
   uploads on its own clock.
2. **One synchronous entry per format.** `loadObj(path)` /
   `loadWav(path)` etc. exist as plain functions that read a file and
   return an `AssetResult<T>`. No registry needed for tools and tests.
3. **No exceptions on the hot path.** Parse errors return
   `AssetResult::failure(code, message)` — the registry's async pool
   does not catch and swallow.
4. **Dedup by canonical key, not by handle.** Two `load("cube.obj")`
   calls from different systems yield two `AssetHandle<MeshData>`s
   pointing at one owning record. Refcount is on the record.
5. **Idempotent reload.** Hot-reload swaps the *content* of the
   owning record in place; existing handles stay valid. The engine
   bridge emits `AssetReloaded` so subscribers can re-upload to the
   GPU.
6. **Zero-alloc steady state.** After warmup, polling the registry
   (`tryGet`, `refCount`, `pendingCount`) and pumping the async loader
   for the no-op case (no pending requests) is zero heap traffic.
7. **No engine coupling by default.** The static lib `threadmaxx::assets`
   links nothing but the STL. The engine bridge (A7) is opt-in via a
   `TARGET threadmaxx::threadmaxx` check, mirrors the
   `THREADMAXX_INPUT_HAS_UI_BRIDGE` pattern.
8. **Cooked bundles are versioned and tagged.** `'TMAS'` magic + `u32`
   version, same caveats as the engine's `WorldSnapshot`: host-endian
   POD, intended for shipping a build of the same binary, not long-
   term archival.
9. **Deterministic.** Two parses of the same byte stream produce
   byte-identical POD output. Cooked-bundle round-trip is byte-
   identical. Hot-reload only mutates a record when the content hash
   actually changes.
10. **Multi-context, multi-registry.** A tool may run multiple
    `AssetRegistry` instances side-by-side (one per project root).
    Registries are independent; canonical keys are scoped to the
    registry that owns them.

## 3. Package layout

```
include/threadmaxx_assets/
  threadmaxx_assets.hpp    # umbrella include
  config.hpp               # capacity caps, dedup hash policy
  types.hpp                # AssetId, AssetType, AssetResult, ErrorCode
  data/
    mesh.hpp               # MeshData, Vertex POD, AABB
    texture.hpp            # TextureData, PixelFormat
    audio.hpp              # AudioClipData, SampleFormat
    font.hpp               # FontAtlas, Glyph, KerningPair
  loaders/
    obj.hpp                # loadObj
    ply.hpp                # loadPly
    bmp.hpp                # loadBmp
    tga.hpp                # loadTga
    png.hpp                # loadPng
    wav.hpp                # loadWav
    bmfont.hpp             # loadBmfont
  registry.hpp             # AssetRegistry, AssetHandle<T>
  async_loader.hpp         # AsyncLoader (worker pool)
  bundle.hpp               # cooked-bundle write/read
  watch.hpp                # FilesystemWatcher (poll-based)
  engine_bridge.hpp        # IResourceLoader bridge (opt-in)
  detail/
    io.hpp                 # ReadFileResult, readFile / readFileInto
    hash.hpp               # FNV-1a-64 for content + path
    inflate.hpp            # minimal RFC1951 DEFLATE decoder (PNG)
    bytes.hpp              # endian-safe little-endian readers
  version.hpp              # version macros + version_string()

src/threadmaxx_assets/
  Registry.cpp
  AsyncLoader.cpp
  Bundle.cpp
  Watch.cpp
  EngineBridge.cpp         # opt-in; only compiled with threadmaxx::threadmaxx
  loaders/
    Obj.cpp
    Ply.cpp
    Bmp.cpp
    Tga.cpp
    Png.cpp
    Wav.cpp
    Bmfont.cpp
  detail/
    Inflate.cpp
tests/assets/
  test_assets_*.cpp
examples/assets_demo/
  main.cpp
```

The library produces a static lib `threadmaxx::assets`. The engine
bridge translation unit + header are added to the target only when
`threadmaxx::threadmaxx` is a known CMake target; that compile-flag
gate exports `THREADMAXX_ASSETS_HAS_ENGINE_BRIDGE=1` so consumers can
`#if`-gate.

## 4. Core data model

### 4.1 IDs, error shape

```cpp
namespace threadmaxx::assets {

// Canonical 32-bit slot id; stable for the lifetime of the registry.
using AssetId = std::uint32_t;
inline constexpr AssetId kInvalidAssetId = 0xFFFFFFFFu;

enum class AssetType : std::uint8_t {
    Unknown = 0,
    Mesh,
    Texture,
    Audio,
    Font,
    Bundle
};

enum class ErrorCode : std::uint16_t {
    Ok = 0,
    FileNotFound,
    IoError,
    BadMagic,
    UnsupportedVersion,
    UnsupportedFormat,
    Truncated,
    ParseError,
    OutOfMemory,
    HashMismatch
};

template <class T>
struct AssetResult {
    T value{};
    ErrorCode code{ErrorCode::Ok};
    std::string message{};      // empty on success; one short line on error

    [[nodiscard]] bool ok() const noexcept { return code == ErrorCode::Ok; }
    static AssetResult success(T v) { return {std::move(v), ErrorCode::Ok, {}}; }
    static AssetResult failure(ErrorCode c, std::string msg) {
        return {T{}, c, std::move(msg)};
    }
};

} // namespace threadmaxx::assets
```

`AssetResult<T>` is the per-loader return shape. The registry layer
exposes `AssetHandle<T>` (refcounted; below) so callers don't pass
around the heavy POD by value.

### 4.2 Mesh POD

```cpp
namespace threadmaxx::assets {

struct Aabb { float min[3]{}; float max[3]{}; };

struct MeshVertex {
    float position[3];
    float normal[3];
    float uv[2];
};
static_assert(sizeof(MeshVertex) == 32);

struct MeshSubmesh {
    std::uint32_t firstIndex{};
    std::uint32_t indexCount{};
    std::uint32_t materialIndex{kInvalidAssetId};   // index into materials
};

struct MeshData {
    std::vector<MeshVertex>  vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshSubmesh> submeshes;
    Aabb aabb;
    std::string sourcePath;     // round-tripped through cook bundles
};

} // namespace threadmaxx::assets
```

Submeshes correspond to OBJ `usemtl` groups / PLY split groups. v1.0
makes no attempt at quad triangulation beyond fan-around-vertex-0; the
demo assets are already triangulated.

### 4.3 Texture POD

```cpp
namespace threadmaxx::assets {

enum class PixelFormat : std::uint8_t {
    Unknown = 0,
    R8,
    RG8,
    RGB8,
    RGBA8,
};

struct TextureData {
    std::uint32_t width{};
    std::uint32_t height{};
    PixelFormat   format{PixelFormat::Unknown};
    bool          srgb{true};
    std::vector<std::byte> pixels;      // row-major, top-left origin
    std::string   sourcePath;
};

} // namespace threadmaxx::assets
```

Top-left origin matches Vulkan's convention. TGA files (bottom-left)
are flipped at parse time. v1.0 does NOT generate mips — the renderer
does that on upload.

### 4.4 Audio POD

```cpp
namespace threadmaxx::assets {

enum class SampleFormat : std::uint8_t {
    Unknown = 0,
    PcmS16,
    PcmF32
};

struct AudioClipData {
    std::uint32_t sampleRate{};
    std::uint16_t channels{};   // 1 = mono, 2 = stereo
    SampleFormat  format{SampleFormat::Unknown};
    std::vector<std::byte> samples;     // interleaved
    std::string sourcePath;

    // Convenience computed view; valid only when samples.size() matches.
    std::uint64_t sampleFrames() const noexcept;
    double durationSeconds() const noexcept;
};

} // namespace threadmaxx::assets
```

WAV files with non-PCM cookie (compressed) return `UnsupportedFormat`.

### 4.5 Font POD

```cpp
namespace threadmaxx::assets {

struct FontGlyph {
    std::uint32_t codepoint{};
    std::uint16_t x{}, y{};       // page atlas pixel coords
    std::uint16_t w{}, h{};
    std::int16_t  xOffset{}, yOffset{};
    std::int16_t  xAdvance{};
    std::uint8_t  page{};
};

struct FontKerning {
    std::uint32_t first{};
    std::uint32_t second{};
    std::int16_t  amount{};
};

struct FontAtlas {
    std::string  fontName;
    std::uint16_t fontSize{};
    std::uint16_t lineHeight{};
    std::uint16_t base{};
    std::vector<TextureData> pages;     // one per .png the .fnt references
    std::vector<FontGlyph>   glyphs;
    std::vector<FontKerning> kernings;
    std::string sourcePath;
};

} // namespace threadmaxx::assets
```

BMFont's `.fnt` carries per-page texture filenames. The loader resolves
them relative to the `.fnt` path and parses each page through the PNG
loader (A3); page count is whatever the font ships.

## 5. Public API

### 5.1 Synchronous loaders

```cpp
namespace threadmaxx::assets {

AssetResult<MeshData>    loadObj   (std::string_view path);
AssetResult<MeshData>    loadPly   (std::string_view path);
AssetResult<TextureData> loadBmp   (std::string_view path);
AssetResult<TextureData> loadTga   (std::string_view path);
AssetResult<TextureData> loadPng   (std::string_view path);
AssetResult<AudioClipData> loadWav (std::string_view path);
AssetResult<FontAtlas>   loadBmfont(std::string_view path);

// In-memory variants for tests / hot-reload paths where the caller
// already has the bytes.
AssetResult<MeshData>    parseObj   (std::span<const std::byte> bytes,
                                     std::string_view sourcePath = {});
// ... matching parseXxx for every format.

} // namespace threadmaxx::assets
```

Every loader uses the same `detail::readFile` indirection — overridable
at link time for tests that want to inject a memory-backed FS.

### 5.2 Registry

```cpp
namespace threadmaxx::assets {

template <class T>
class AssetHandle {
public:
    AssetHandle() noexcept = default;
    AssetHandle(const AssetHandle&) noexcept;
    AssetHandle(AssetHandle&&) noexcept;
    AssetHandle& operator=(const AssetHandle&) noexcept;
    AssetHandle& operator=(AssetHandle&&) noexcept;
    ~AssetHandle();

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] AssetId id() const noexcept;
    [[nodiscard]] const T* get() const noexcept;     // nullptr if invalid
    [[nodiscard]] const T& operator*() const noexcept;
    [[nodiscard]] const T* operator->() const noexcept;
};

class AssetRegistry {
public:
    AssetRegistry();
    ~AssetRegistry();
    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;

    // Synchronous load + dedup. Identical canonical paths return the same
    // slot; refcount goes up by one per call.
    AssetHandle<MeshData>      loadMesh    (std::string_view path);
    AssetHandle<TextureData>   loadTexture (std::string_view path);
    AssetHandle<AudioClipData> loadAudio   (std::string_view path);
    AssetHandle<FontAtlas>     loadFont    (std::string_view path);

    // Inject pre-built data (cooked bundles, generated assets).
    AssetHandle<MeshData>    addMesh   (std::string_view name, MeshData v);
    AssetHandle<TextureData> addTexture(std::string_view name, TextureData v);

    // Force a reload from the original source path. Existing handles
    // stay valid; pointed-at content is replaced atomically (move-assign
    // under the slot mutex).
    bool reload(AssetId id);

    // Diagnostic / engine bridge.
    [[nodiscard]] std::size_t liveAssetCount(AssetType t = AssetType::Unknown) const;
    [[nodiscard]] std::size_t refCount(AssetId id) const;
    [[nodiscard]] AssetType   typeOf(AssetId id) const;
    [[nodiscard]] std::optional<std::string> pathOf(AssetId id) const;

    struct Stats {
        std::uint64_t loadsSync{};
        std::uint64_t loadsDedup{};
        std::uint64_t reloads{};
        std::uint64_t evicted{};
    };
    [[nodiscard]] Stats stats() const;
};

} // namespace threadmaxx::assets
```

Refcount is bumped on `loadX` (dedup or fresh) and on `AssetHandle`
copy. Released to zero → slot freed + generation bumped; the engine-
bridge `IResourceLoader` reuses this for `ResourceHandle<T>` lifetime
hand-off.

### 5.3 Async loader

```cpp
namespace threadmaxx::assets {

class AsyncLoader {
public:
    explicit AsyncLoader(AssetRegistry& reg,
                         std::size_t workerCount = 2);
    ~AsyncLoader();

    // Enqueue a load. Returns immediately with a handle that initially
    // reports valid()==false; flips to true after pump() observes the
    // future complete and installs the asset in the registry.
    AssetHandle<MeshData>      enqueueMesh   (std::string_view path);
    AssetHandle<TextureData>   enqueueTexture(std::string_view path);
    AssetHandle<AudioClipData> enqueueAudio  (std::string_view path);
    AssetHandle<FontAtlas>     enqueueFont   (std::string_view path);

    // Drains completed futures on the calling thread (typically the
    // sim thread). Zero-alloc when no requests are pending.
    void pump();

    [[nodiscard]] std::size_t pendingCount() const noexcept;
    [[nodiscard]] std::size_t inFlightCount() const noexcept;
    [[nodiscard]] std::size_t failedCount() const noexcept;
};

} // namespace threadmaxx::assets
```

The async loader owns its own `std::thread` pool. Engine doesn't drive
worker creation. `pump()` is the host's responsibility — call it once
per frame, or from the engine bridge's `update()` hook (A7).

### 5.4 Cooked bundle

```cpp
namespace threadmaxx::assets {

// Single cooked-bundle file = list of (type, name, blob) entries.
struct Bundle {
    std::vector<MeshData>       meshes;
    std::vector<TextureData>    textures;
    std::vector<AudioClipData>  audio;
    std::vector<FontAtlas>      fonts;
};

AssetResult<std::vector<std::byte>> writeBundle(const Bundle& b);
AssetResult<Bundle> readBundle(std::span<const std::byte> bytes);
AssetResult<Bundle> readBundleFromFile(std::string_view path);

// Convenience: load the whole bundle into a registry under name keys.
void mountBundleInto(AssetRegistry& reg, const Bundle& b,
                     std::string_view prefix = {});

} // namespace threadmaxx::assets
```

Format: `[magic 'TMAS' u32][version u32][meshCount u32][textureCount
u32][audioCount u32][fontCount u32]` followed by serialized records in
that order. Each record is `[nameLen u32][name bytes][POD payload]`.
Magic chosen to avoid collisions with the engine's `'SXMT'`, the input
lib's `'TMIN'`/`'TMIB'`, and tou2d's `'KEY '`.

### 5.5 Filesystem watcher

```cpp
namespace threadmaxx::assets {

class FilesystemWatcher {
public:
    explicit FilesystemWatcher(double pollIntervalSeconds = 1.0);
    ~FilesystemWatcher();

    void watch(std::string_view path);
    void unwatch(std::string_view path);

    // Returns paths whose mtime changed since the last poll. Caller
    // typically pipes these into AssetRegistry::reload(id).
    std::span<const std::string> tick();
};

} // namespace threadmaxx::assets
```

Poll-based by design: `inotify` / `kqueue` / `ReadDirectoryChangesW`
are platform-specific and require their own threads; the polling
fallback keeps the v1.0 surface portable. The host gates how often
`tick()` is called.

### 5.6 Engine bridge

```cpp
namespace threadmaxx::assets {

// Opt-in: only compiled when threadmaxx::threadmaxx is a known target.
// Registers the assets registry + async loader as a
// threadmaxx::IResourceLoader: pump() runs from update(), and any
// reloads from FilesystemWatcher are forwarded as AssetReloaded events
// on the engine's event channel.
#if THREADMAXX_ASSETS_HAS_ENGINE_BRIDGE
class EngineAssetLoader : public threadmaxx::IResourceLoader {
public:
    EngineAssetLoader(AssetRegistry& reg,
                      AsyncLoader& asyncLoader,
                      FilesystemWatcher* watcher = nullptr);

    void update(threadmaxx::Engine& engine) override;
    threadmaxx::LoaderStats stats() const override;
};
#endif

} // namespace threadmaxx::assets
```

The bridge file is the single place that includes `<threadmaxx/Engine.hpp>`
and `<threadmaxx/Resource.hpp>`. CI checks the no-engine-link gate in A1
to keep this contained.

## 6. Implementation notes

### 6.1 File reading

`detail::io::readFile(path)` is the single I/O entry. It opens via
`std::ifstream` in binary mode, reserves to the file size, reads in one
shot, and returns `AssetResult<std::vector<std::byte>>`. Loaders
never touch `<fstream>` directly — that lets tests substitute an
in-memory backend by linking a different `detail::io::readFile`
translation unit.

### 6.2 OBJ parsing

Single-pass tokenizer keyed off the leading line tag (`v`, `vn`, `vt`,
`f`, `usemtl`, `g`, `o`, `mtllib`). Face indices `a/b/c` and `a//c`
both supported; `a` (position-only) generates a flat default normal
across the polygon. Vertices are deduplicated by `(pos_idx, norm_idx,
uv_idx)` triple via an open-addressing hashmap keyed by `uint64_t`
packed key. Quad faces are fanned around vertex 0 (the demo assets
are pre-triangulated anyway).

Missing normals (`vn` count == 0) → second pass computes smoothed
per-vertex normals (sum of face normals, normalize). Missing uvs
default to `(0, 0)`.

Materials referenced by `usemtl` create new submeshes — material names
are kept as strings; v1.0 does not parse `.mtl` files (the rpg_demo
materials are programmatic).

### 6.3 PLY parsing

Header is ASCII; body is either ASCII (`format ascii 1.0`) or binary
little-endian (`format binary_little_endian 1.0`). v1.0 supports only
the latter two — big-endian PLY returns `UnsupportedFormat`. Property
list type is restricted to `uchar int` (the common Stanford bunny
format). Loader walks `element vertex N` for positions / optional
normals / optional uvs, then `element face M` for triangle / quad
indices, fans quads same as OBJ.

### 6.4 BMP / TGA / PNG

- **BMP**: only `BITMAPINFOHEADER` (40-byte) header. 24-bit and 32-bit
  uncompressed (compression == 0). Rows padded to 4-byte alignment in
  the file; loader strips padding. BMP is bottom-up; the loader
  flips at parse.
- **TGA**: types 2 (uncompressed RGB), 3 (uncompressed grayscale), 10
  (RLE RGB). 24 / 32 bpp for type 2 / 10. Origin bit decoded from the
  image descriptor.
- **PNG**: minimal IHDR + IDAT + IEND. Color types `2` (RGB) and `6`
  (RGBA) only; bit depth fixed at 8; interlace not supported. The
  IDAT zlib stream is inflated via `detail::inflate.cpp` (RFC1950 +
  RFC1951) — embedded, not linked. PNG filter byte per scanline is
  reversed in place.

The PNG inflate is the heaviest piece of code in the library; it's
self-contained and unit-tested independently.

### 6.5 WAV

RIFF parser: reads `'RIFF' <size> 'WAVE'`, then walks chunks. `fmt `
chunk must come first (per the spec). `wFormatTag = 1` → PCM 16-bit;
`wFormatTag = 3` → IEEE float32. Other formats return
`UnsupportedFormat`. `data` chunk is the sample payload. Multi-chunk
WAVs (with `LIST`, `bext`) are tolerated — unknown chunks are skipped.

### 6.6 BMFont

`.fnt` text format starts with `info ...` / `common ...` / `page ...`
lines; key/value tokenizer parses each. The binary format is the
3-byte magic `'B', 'M', 'F'` + version 3 + block headers; loader
dispatches on the first byte. Page filenames are resolved relative to
the `.fnt` path; the matching PNG is loaded through `loadPng`. Glyphs
are stored in registration order; kerning pairs are sorted on load so
binary search is the lookup path.

### 6.7 Registry layout

```cpp
struct AssetSlot {
    AssetId   id{kInvalidAssetId};
    AssetType type{AssetType::Unknown};
    std::uint32_t generation{};
    std::atomic<std::uint32_t> refCount{};
    std::string canonicalPath;
    std::uint64_t contentHash{};
    std::shared_ptr<void> data;       // owns MeshData / TextureData / ...
};
```

Slots live in a `std::vector<AssetSlot>`; freed slots go on an int free
list. `canonicalPath` is `std::filesystem::weakly_canonical` —
case-folded, symlink-resolved. The keyed lookup map is
`std::unordered_map<std::string, AssetId>` (canonical path → slot).
Behind a single `std::shared_mutex`: writers (load / reload / refcount
zero) lock exclusive; readers (`tryGet`, `refCount`, `pathOf`) lock
shared. Read-heavy hot path — handle copy is a single relaxed atomic
increment via `AssetSlot::refCount`.

### 6.8 Async loader

Worker threads loop on `std::condition_variable` + queue of
`std::function<AssetResult<void*>()>` (type-erased). `pump()` drains a
per-loader `std::mutex`-protected vector of ready records and installs
them into the registry. Handles enqueued through the async loader are
*reserved* immediately — slot is allocated, generation set, data
pointer is `nullptr` until pump installs. `AssetHandle<T>::valid()`
returns false until then.

The worker pool size defaults to `min(2, hardware_concurrency())` — no
reason for a 16-thread fanout on what's almost always disk-bound work.

### 6.9 Zero-alloc steady state

- `AssetRegistry::tryGet` / `refCount` / `pathOf` — pure reads under
  the shared mutex, no allocation.
- `AsyncLoader::pump` — empty fast path (`pendingResults_.empty()`
  check under one quick lock) is alloc-free.
- `AssetHandle` copy / move — single atomic op.
- `FilesystemWatcher::tick` — `std::vector<std::string>` returned by
  span; the watcher reuses the same vector across calls.

Pinned by the v1.0 crowd no-alloc gate (close-out): 1000 meshes /
1000 textures / 100 handle copies per "frame" over 100 measured frames
after warmup, zero heap traffic.

### 6.10 Determinism

Two parses of the same bytes return byte-identical PODs. The OBJ
vertex-dedup pass is order-sensitive (we hash position/normal/uv
triples in face-walking order); we lock that down with a golden-bytes
test (parse cube.obj → check vertex/index arrays byte-for-byte
against a recorded reference).

Bundle round-trip is byte-identical: serialize → deserialize →
serialize produces the same bytes back. Pinned by a test in A8.

### 6.11 No-engine-coupling check

`threadmaxx_assets` does not transitively include any `threadmaxx/`
core header. The engine-bridge file (`EngineBridge.cpp`) is the
single exception and is gated by the CMake `TARGET threadmaxx::threadmaxx`
check. CI checks the include graph in A1's foundation test.

## 7. Suggested implementation order

Eight shippable batches plus a v1.0 close-out. Numbering is `Ak`.

1. **A1 — Foundations.** `AssetId`, `AssetType`, `AssetResult`,
   `ErrorCode`, the in-memory POD declarations
   (`MeshData` / `TextureData` / `AudioClipData` / `FontAtlas`),
   `detail::io::readFile`, `version.hpp`, no-engine-link gate test.
2. **A2 — Mesh importers.** `loadObj` (triangulation, vertex dedup,
   smoothed-normals fallback), `loadPly` (ASCII + binary little-
   endian), `parseObj` / `parsePly` for in-memory variants, golden-
   bytes test for the cube.
3. **A3 — Texture importers.** `loadBmp` (24/32-bit), `loadTga`
   (uncompressed + RLE), `loadPng` (color types 2 + 6, 8-bit). PNG
   uses the in-tree inflate decoder; round-trip test against a
   recorded baseline.
4. **A4 — Audio importers.** `loadWav` (PCM 16-bit + float32),
   `parseWav`, sample-count / duration helpers.
5. **A5 — Font importers.** `loadBmfont` text + binary, multi-page
   atlas via `loadPng`, kerning binary-search.
6. **A6 — Registry + dedup.** `AssetRegistry`, `AssetHandle<T>`,
   canonical-path dedup, refcount, `reload()`, stats. Locks down
   the slot generation contract.
7. **A7 — Async loader + engine bridge.** `AsyncLoader` (worker
   pool, futures, pump). `EngineAssetLoader` opt-in via
   `TARGET threadmaxx::threadmaxx`; emits `AssetReloaded` via the
   engine's event channel.
8. **A8 — Cooked bundle + hot reload.** `writeBundle` / `readBundle`,
   `mountBundleInto`. `FilesystemWatcher` polling. Bundle round-trip
   byte-identical test.

Then v1.0 close-out: `version.hpp` bump to `1.0.0`, `README.md`,
`USER_GUIDE.md`, `MAINTAINER_GUIDE.md`, `CHANGELOG.md`, the crowd no-
alloc gate, and an `examples/assets_demo` headless that loads OBJ +
PNG + WAV + BMFont, writes a bundle, reads it back, and exits 0.

## 8. Open questions intentionally left for batches

- **`.mtl` parsing.** A2 ships submesh material *names* only — the
  rpg_demo uses programmatic materials, so the full `.mtl` parser is
  v1.x. Submesh `materialIndex` is reserved.
- **`tinyobjloader`-style group lookup.** Object / group hierarchy
  (`o ...` / `g ...`) is read but not emitted as a hierarchy in v1.0;
  submeshes are the only structure exposed.
- **PNG paletted / 16-bit / interlaced.** Out of scope. Rare in game
  assets; opt-in v1.x.
- **JPEG / EXR / KTX.** Out of scope. The renderer expects raw bytes,
  not compressed source — KTX in particular lives one level above
  this library (engine renderer's GPU-format choice).
- **Compressed audio (OGG/Vorbis, MP3, Opus).** Sibling decode is
  heavy and license-fraught; v1.x.
- **TrueType / OpenType rasterization.** Out of scope. BMFont's
  pre-rasterized atlas covers the engine's current needs (rpg_demo
  HUD, tou2d UI). Adding a stbtt-style rasterizer is a v1.x batch.
- **Memory budgets / eviction policy.** A6 ships LRU-free refcount
  semantics; explicit budget caps + LRU eviction are v1.x.
- **Streaming.** Out of scope for v1.0. The bundle format leaves
  room for chunk-level offsets so a streamer can land later without
  a major bump.
