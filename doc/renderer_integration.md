# Renderer Integration

@page renderer_integration Renderer Integration

threadmaxx is renderer-agnostic: it produces a `RenderFrame` after each
commit and hands it to whatever `IRenderer` you plug in. Headless is the
default; the renderer is optional throughout.

## The contract

```cpp
class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual bool initialize();                       // optional
    virtual void shutdown();                         // optional
    virtual void submitFrame(const RenderFrame&) = 0;
};
```

Set the renderer from `IGame::onSetup`:

```cpp
void onSetup(Engine& engine, World&, CommandBuffer&) override {
    renderer_ = std::make_unique<MyRenderer>(...);
    engine.setRenderer(renderer_.get());
}
```

The engine borrows the pointer; you keep ownership. The renderer must
outlive the engine.

A null renderer is allowed. The engine simply skips `submitFrame()`
calls — useful for tests, dedicated servers, and CI.

## The frame

`RenderFrame` exposes **two consumption paths** that coexist:

1. **Flat `instances`** — auto-populated by the engine from every entity
   with a `RenderTag` presence bit and without `DisabledTag`. This is
   the original Milestone-1 contract — headless / minimal renderers can
   keep using just this.
2. **Hierarchical fields** — `cameras`, `lights`, per-pass `drawItems`,
   `debugLines` / `debugPoints` / `debugText`. Populated by user
   systems via the @ref ISystem::buildRenderFrame hook (§3.2 batch 8).
   The engine merges every system's contribution in registration order
   on the simulation thread, so the published spans are deterministic
   for a given system set.

```cpp
struct RenderFrame {
    std::uint64_t tick;
    double simulationTime;
    double deltaTime;
    float alpha;

    // Auto-populated flat instance list (RenderTag + not DisabledTag).
    std::span<const RenderInstance> instances;

    // Hierarchical scene (filled by user systems via buildRenderFrame).
    std::span<const Camera> cameras;
    std::span<const Light>  lights;
    std::array<std::span<const DrawItem>, kRenderPassCount> drawItems;
    std::span<const DebugLine>  debugLines;
    std::span<const DebugPoint> debugPoints;
    std::span<const DebugText>  debugText;
};
```

All spans are **engine-owned memory**, valid for the duration of
`submitFrame()`. The renderer must finish reading them before returning,
or copy what it needs.

### Per-pass draw items

```cpp
enum class RenderPass : std::uint8_t {
    Opaque        = 0,
    Transparent   = 1,
    ShadowCasters = 2,
    Overlay       = 3,
};
```

Renderers index the per-pass bins via `frame.drawItems[passIndex(p)]`.
Pass slots are renderer-neutral: a renderer that doesn't support shadows
simply ignores `ShadowCasters`. Renderers that want more bins can layer
their own classification on top of `DrawItem::flags` / `DrawItem::sortKey`.

### Populating the frame from a system

```cpp
class CameraSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "camera"; }
    void update(threadmaxx::SystemContext&) override {}  // no per-tick work

    void buildRenderFrame(threadmaxx::RenderFrameBuilder& b) override {
        threadmaxx::Camera cam;
        cam.fovYRadians = 1.0472f;
        cam.view = computeView();
        cam.projection = computeProj();
        b.addCamera(cam);

        threadmaxx::Light sun;
        sun.type = threadmaxx::LightType::Directional;
        sun.direction = {-0.3f, -1.0f, -0.2f};
        sun.castsShadow = true;
        b.addLight(sun);

        for (const auto& e : visibleEntities_) {
            threadmaxx::DrawItem item;
            item.entity = e.handle;
            item.transform = e.transform;
            item.meshId = e.meshId;
            b.addDrawItem(threadmaxx::RenderPass::Opaque, item);
        }
    }
};
```

The builder is exclusive to this system for the duration of the call —
no synchronization is needed. Allocations inside `addCamera` / `addLight`
/ `addDrawItem` are amortized across ticks (the engine retains the
underlying storage).

### Visibility culling

`include/threadmaxx/render/Visibility.hpp` ships header-only helpers for
the common case: AABB-vs-frustum tests with the standard 6-plane
extraction.

```cpp
auto frustum = threadmaxx::extractFrustum(camera);
if (threadmaxx::intersectsFrustum(frustum, aabb.min, aabb.max)) {
    builder.addDrawItem(threadmaxx::RenderPass::Opaque, item);
}

// Or batched: writes each item's cameraMask in place.
threadmaxx::cullByFrustum(items, bounds, cameras);
```

Up to 32 cameras supported per pass via `DrawItem::cameraMask`. A
renderer iterating camera `c` filters its bin by `(cameraMask >> c) & 1`.

### Instance buffer layout helper

`include/threadmaxx/render/InstanceBufferLayout.hpp` packs a `DrawItem`
into a stable 128-byte `std140`-friendly entry that any GPU backend can
upload directly:

```cpp
threadmaxx::InstanceLayoutEntry entry = threadmaxx::packInstance(item);
// or batched into a destination span (e.g. a mapped GPU buffer):
threadmaxx::packInstances(frame.drawItems[0], mappedSlice);
```

Layout is 16-byte aligned with predictable field order
(`worldPosition`, `worldOrientation`, `worldScale`, `materialOverride`,
ids, flags, sort-key halves, entity index, 32 bytes reserved). Renderers
with custom layout needs ignore the helper and read raw `DrawItem`
fields.

### Per-frame upload ring

`include/threadmaxx/render/UploadRing.hpp` is a tiny renderer-neutral
bump allocator for per-frame staging:

```cpp
threadmaxx::UploadRing ring(/*frameCount*/3, /*bytesPerFrame*/1u << 20);

// At the start of each rendered frame:
ring.nextFrame();

// Push per-instance data, get a writable pointer:
auto* dst = static_cast<MyInstance*>(ring.allocate(sizeof(MyInstance) * n));
// ...or copy bytes and get back the offset (for GPU-buffer mapping):
auto offset = ring.pushBytes(data.data(), data.size());
```

Works for Vulkan (one `VkBuffer` per in-flight frame), D3D12 UPLOAD
heaps, WebGPU mapped buffers, or pure-software backends. Not
thread-safe; one ring per render thread if you parallelize.

### Auto-populated flat instances

`frame.instances` is unchanged from Milestone 1: one `RenderInstance`
per live entity with the `RenderTag` presence bit (and without
`DisabledTag`). Headless renderers and minimal examples consume this
without touching the hierarchical fields.

## How publication works

The engine double-buffers the render frame: there are two `RenderFrame`
slots, two `std::vector<RenderInstance>` slots, and an atomic
`frontIndex_`. After each commit the engine writes into the back slot
and publishes:

```cpp
back = 1 - front;
buildRenderFrame(back);
frontIndex_.store(back, std::memory_order_release);
renderer_->submitFrame(renderFrames_[back]);
```

Today `submitFrame` is called synchronously from the simulation thread
immediately after the swap. Single-threaded renderers don't need to
worry about the atomic at all — by the time they read `frame.instances`
the publishing thread is the same one. If a future renderer reads from
another thread, the release/acquire on `frontIndex_` is the
synchronization point.

## Interpolation

`RenderFrame::alpha` is the wall-clock fraction (0..1) past the last
committed tick. Two modes:

- **`step()`** always submits the post-tick frame with `alpha = 0`. Use
  this if you drive the loop yourself and want raw, tick-aligned state.
- **`run()`** runs zero-to-many `step()` calls per outer iteration to
  catch up to wall-clock, then makes one extra call to
  `submitInterpolatedFrame(alpha)` which mutates `alpha` on the current
  front frame and re-submits. World state is unchanged between sim ticks,
  so the engine doesn't rebuild `instances` — it just updates `alpha`.

So in `run()` mode the renderer sees the same `instances` data twice in
the same wall-clock tick: once at `alpha = 0` from the commit and once
at `alpha = wallClockFraction` from the interpolation pass.

How to actually use `alpha`: cache the previous frame's `Transform`
keyed by `EntityHandle`, then lerp `prev -> current` by `alpha` in your
draw call. The `examples/boids` SDL2 renderer does this; copy from
there.

## Worked example

The minimal example's `ConsoleRenderer` does the smallest thing that
counts as "using a frame":

```cpp
void ConsoleRenderer::submitFrame(const threadmaxx::RenderFrame& frame) {
    ++frameCount_;
    if (frame.tick % printEvery_ == 0) {
        std::printf("[frame] tick=%llu instances=%zu alpha=%.2f\n",
                    static_cast<unsigned long long>(frame.tick),
                    frame.instances.size(),
                    static_cast<double>(frame.alpha));
    }
}
```

The boids example layers SDL2 on top: it opens a window in
`initialize()`, draws each `RenderInstance` as a triangle oriented along
its velocity (which it looks up via `EntityHandle` from the world), and
processes window events from a side thread. The renderer itself never
mutates world state — input is published via an event channel the game
reads in a system.

## What renderers should not do

- **Don't retain raw pointers from `frame.instances` across calls.** The
  memory may be reused on the next tick.
- **Don't call `Engine::step()` or anything that mutates the world** from
  inside `submitFrame()`. The simulation thread is sitting in your call
  and won't deadlock, but you'll re-enter the engine with half-published
  state.
- **Don't take a long time inside `submitFrame` if `Config::sleepToPace
  = true`.** The engine assumes the renderer returns quickly; long
  GPU stalls eat into the tick budget. If your renderer needs to wait
  for a GPU fence, do it on its own thread and `submitFrame` is just
  the producer side of a queue.
- **Don't assume `frame.tick` is monotonically `+1`.** In `run()` mode
  the engine may submit two frames for the same tick (commit + interp).
  `alpha` distinguishes them.

## Plugging in a real backend

The boids SDL2 renderer is the smallest non-trivial example of plugging
in a windowed backend. For something heavier (Vulkan, Metal, D3D12):

- Implement `initialize()` to set up the swapchain, descriptor pools,
  command pool, etc. Return false to abort startup.
- In `submitFrame`, build draw lists from `frame.instances`, submit
  command buffers, present.
- In `shutdown()`, teardown in the right order. Note `shutdown` runs
  after the engine has stopped issuing frames, so you can fence-wait on
  any in-flight GPU work safely.

If your backend needs more than the flat `instances` list provides —
light lists, shadow casters, multiple cameras, post-process inputs —
populate the hierarchical fields via the @ref ISystem::buildRenderFrame
hook. The §3.2 batch-8 surface above is the supported way to do this;
see `tests/render_frame_builder_test.cpp` and `tests/render_passes_test.cpp`
for end-to-end examples.
