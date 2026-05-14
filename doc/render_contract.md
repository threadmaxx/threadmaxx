# Render contract (§3.2 batch 8)

@page render_contract Render contract

The hierarchical render contract introduced in §3.2 batch 8 (2026-05-14)
moves rendering from a single flat `instances` list to a structured
`RenderFrame` with cameras, lights, per-pass draw items, and debug
geometry. The engine remains renderer-agnostic — every type in this
contract is a renderer-neutral POD; Vulkan, D3D12, WebGPU, or a pure-
software backend can consume them without translation.

## The shape

```
RenderFrame
├── instances   (auto, flat — legacy Milestone-1 path)
├── cameras     (user-populated via buildRenderFrame hook)
├── lights      ( "" )
├── drawItems[ Opaque | Transparent | ShadowCasters | Overlay ]
├── debugLines
├── debugPoints
└── debugText
```

All spans are engine-owned; valid for the duration of `submitFrame()`.
The renderer copies or finishes reading before returning.

## Types

The public surface lives under `include/threadmaxx/render/`:

| Header                          | Purpose |
|---------------------------------|---------|
| `Camera.hpp`                    | POD camera (projection mode, view + projection matrices) |
| `Light.hpp`                     | POD light (directional / point / spot) |
| `DrawItem.hpp`                  | POD draw item (transform, mesh, material, skinning ref, camera mask) |
| `DebugGeometry.hpp`             | `DebugLine`, `DebugPoint`, `DebugText` |
| `RenderPasses.hpp`              | `RenderPass` enum + `passIndex` helper |
| `RenderFrameBuilder.hpp`        | Per-system slice consumed by the engine's merge pass |
| `Visibility.hpp`                | Frustum extraction + AABB tests + `cullByFrustum` |
| `InstanceBufferLayout.hpp`      | Stable 128-byte std140-friendly per-instance entry |
| `UploadRing.hpp`                | Header-only frame-to-frame staging arena |

These are all included by the umbrella header
`<threadmaxx/threadmaxx.hpp>`; you can pull just what you need.

## Why PODs and not built-in components?

The original batch-8 spec called Camera / Light / MeshSkinned /
AnimationPose / MaterialOverride out as "new built-in components."
Implementing them via the §3.1 11-step recipe would wedge render-side
data into the per-entity dense parallel arrays of `EntityStorage`.

The shipped design keeps them as render-side PODs that flow through
the `RenderFrameBuilder` for several reasons:

- **Cameras and lights are typically few in number.** A 1000-entity
  dense array of cameras is wasted footprint for the 1-3 cameras a
  scene usually has.
- **`AnimationPose` ringbuffered doesn't fit dense per-entity storage.**
  The pose data is shared across in-flight frames; what the entity
  owns is a reference (`AnimationPoseRef`), which is part of `DrawItem`.
- **It preserves the renderer-agnostic guarantee.** §3.6 of
  FUTURE_WORK.md requires every renderer-facing addition to be
  justified by a non-Vulkan-specific use case first. A POD that any
  backend consumes directly is the cleanest possible fit; making
  these built-in components would bias the engine toward "every
  renderer has cameras attached to entities," which isn't true for
  e.g. a fixed-camera tile renderer or a scene-graph-owned camera
  setup.
- **The hook gives game code more flexibility.** A system can pull
  camera data from the world, from a sidecar service, from
  `UserData`, or from its own state — whatever fits the game's
  architecture.

If a future game does want entity-attached cameras as a built-in
component, the §3.1 batch 6b `UserComponent<T>` extension hook is
the right moment to add that — chunked archetype storage (✅ §3.1
batch 6) already supports per-archetype parallel arrays
efficiently; batch 6b lets game code register additional component
types without forking the engine.

## The `buildRenderFrame` hook

```cpp
class CameraSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "camera"; }
    void update(threadmaxx::SystemContext&) override {}

    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override {
        b.addCamera(makeMainCamera_());
        b.addLight(makeSun_());
        for (const auto& v : visibleMeshes_) {
            b.addDrawItem(threadmaxx::RenderPass::Opaque, v);
        }
    }
};
```

**Invocation order:** after every `postStep` has committed and the
event channels have drained, on the simulation thread, in
registration order. Each registered system gets its own builder; the
engine merges builders in registration order into the back render
frame, then atomically publishes.

**Cost when unused:** one virtual call per system per tick to the
empty default. Negligible.

**Allocations:** the engine retains each builder across ticks, so
steady-state usage pays zero allocations after the first tick.

## Visibility culling

`include/threadmaxx/render/Visibility.hpp` ships a standard 6-plane
frustum extractor and an AABB-vs-frustum p-vertex test, plus a
batch helper that writes each `DrawItem::cameraMask` in place. Up to
32 cameras are supported simultaneously.

```cpp
threadmaxx::Frustum f = threadmaxx::extractFrustum(camera);
const bool visible = threadmaxx::intersectsFrustum(f, aabb.min, aabb.max);

// Or batch over a per-system span:
threadmaxx::cullByFrustum(drawItems, bounds, cameras);
// drawItems[i].cameraMask now has bit c set iff item i is visible to camera c.
```

The library deliberately does **not** ship a built-in `ISystem`
implementation for culling. Visibility is intimately tied to where
your cameras come from and how your game decides which draw items to
emit — better expressed as the few lines inside your own
`buildRenderFrame` hook than as a configurable engine system.
Renderers that want fancier culling (occlusion queries, hierarchical
Z, portal-based) layer it on top of `cameraMask`.

## Instance buffer layout

```cpp
struct alignas(16) InstanceLayoutEntry {
    float worldPosition[4];        // xyz + pad
    float worldOrientation[4];     // quat xyzw
    float worldScale[4];           // xyz + pad
    float materialOverride[4];
    std::int32_t meshId, materialId, skeletonId, poseRingSlot;
    std::uint32_t flags, sortKeyLow, sortKeyHigh, entityIndex;
    float pad[8];                  // reserved
};
static_assert(sizeof(InstanceLayoutEntry) == 128, "");
```

`packInstance(item)` produces one entry; `packInstances(items, dst)`
batches across a span. Renderers with custom layouts ignore this
helper; it's purely a starting point for backends that want a
known-good shader-compatible packing.

## Per-frame upload ring

```cpp
threadmaxx::UploadRing ring(/*frameCount*/3, /*bytesPerFrame*/1u << 20);
ring.nextFrame();
auto* dst = ring.allocate(byteCount, /*alignment*/16);
// or, copy + return offset:
auto offset = ring.pushBytes(srcPtr, srcBytes);
```

Backend-neutral; the renderer maps the slab to a GPU buffer (Vulkan
`VkBuffer`, D3D12 UPLOAD heap, WebGPU mapped buffer, ...) and uses
`head()` / `bytesPerFrame()` to record flush ranges. Not thread-safe;
use one ring per render thread for parallel uploads.

## End-to-end test references

- `tests/render_frame_builder_test.cpp` — registration-order merge,
  builder reset between ticks.
- `tests/build_render_frame_timing_test.cpp` — hook fires after
  postStep, before the next tick's preStep.
- `tests/render_passes_test.cpp` — per-pass bins, debug geometry,
  coexistence with the legacy flat `instances` array.
- `tests/visibility_culling_test.cpp` — frustum extraction,
  intersection test, batched mask population.
- `tests/instance_buffer_layout_test.cpp` — `packInstance` field
  shape, batch packing.
- `tests/upload_ring_test.cpp` — slab rotation, alignment, grow-on-
  overflow toggle.
