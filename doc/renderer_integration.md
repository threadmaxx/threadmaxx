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

```cpp
struct RenderFrame {
    std::uint64_t tick;
    double simulationTime;
    double deltaTime;
    float alpha;                            // 0..1, see "Interpolation" below

    std::span<const RenderInstance> instances;
};

struct RenderInstance {
    EntityHandle entity;
    Transform    transform;
    std::int32_t meshId, materialId;
    std::uint32_t flags;
    std::uint64_t userData;
};
```

`instances` is **engine-owned memory**. The pointer is valid for the
duration of `submitFrame()`. The renderer must finish reading it before
returning, or copy the data it needs.

The engine builds one `RenderInstance` per live entity that has its
`RenderTag` presence bit set. Position, orientation, scale come from the
entity's world `Transform` (post-hierarchy). The mesh/material/flags
fields come straight from `RenderTag`; `userData` is whatever the
entity's `UserData` says.

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

If your backend needs more than `RenderFrame` provides today (light
lists, shadow casters, multiple cameras, post-process inputs), that's a
known limitation called out in [FUTURE_WORK.md](../FUTURE_WORK.md). The
short-term workaround is to compute that data renderer-side from the
engine's flat instance list.
