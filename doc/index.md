# threadmaxx — User Guide

@page user_guide User Guide

threadmaxx is a renderer-agnostic C++20 game backend. Game code registers
*systems* (movement, AI, physics, ...); the engine drives a fixed-step
simulation loop, parallelizes per-system work across a worker pool, and
hands a flat `RenderFrame` to whatever renderer you plug in. There are no
locks in user gameplay code.

This guide is organized as a sequence — skim it once front-to-back to build
the model, then dip back in for reference. The
[Architecture Overview](../ARCHITECTURE.md) is the canonical complement and
documents the invariants this guide relies on; the
[generated API reference](generated/html/index.html) (built via `cmake
--build build --target doc`) covers every public symbol.

## Reading order

1. [Getting started](getting_started.md) — five-minute tour from a fresh
   checkout to a running headless example.
2. [Core concepts](concepts.md) — the engine's mental model in one page:
   world, entities, components, systems, command buffers, waves.
3. [Components & queries](components_and_queries.md) — built-in components,
   the per-entity presence mask, and the `forEach` / `forEachWith`
   helpers.
4. [Systems & scheduling](systems_and_scheduling.md) — implementing
   `ISystem`, declaring read/write sets, and how the engine groups
   non-conflicting systems into concurrent waves.
5. [Lifecycle hooks](lifecycle_hooks.md) — `preStep` / `update` /
   `postStep`, when each fires, and what to put in which.
6. [Command buffers](command_buffers.md) — recording mutations from worker
   jobs and the deterministic commit phase.
7. [Reserved handles](reserved_handles.md) — spawning a parent and a
   child in a single recording via `ctx.reserveHandle()`.
8. [Scratch arenas](scratch_arenas.md) — per-job bump allocator for
   short-lived POD scratch memory.
9. [Hierarchy](hierarchy.md) — the built-in `Parent` component and the
   `HierarchySystem` factory.
10. [Event channels](events.md) — typed double-buffered cross-system
    messaging.
11. [Resources](resources.md) — `ResourceId<T>` and `ResourceRegistry` for
    meshes, textures, audio clips, anything the engine doesn't own.
12. [Renderer integration](renderer_integration.md) — implementing
    `IRenderer`, the double-buffered render frame, and the interpolation
    alpha.
13. [Configuration & lifecycle](configuration.md) — `Config` fields, the
    engine lifecycle, deterministic mode, fixed timestep, pacing.
14. [Pause and time scale](pause_and_time_scale.md) — slow-mo,
    pause-menu semantics, deterministic-tick interaction.
15. [Stats & profiling](stats_and_profiling.md) — `EngineStats` and per-
    `SystemStats`, what they measure and how to wire them into a HUD.

## What this guide does not cover

- The internal storage layout, the wave scheduler implementation, or the
  work-stealing job queue — see [ARCHITECTURE.md](../ARCHITECTURE.md) for
  the design and `src/` for the code.
- Renderer-side concerns (Vulkan, OpenGL, …). The engine speaks
  `RenderFrame`; what you do with it is your library's problem.
- A specific build of a game. There are two examples (`examples/minimal`
  headless, `examples/boids` with SDL2) that illustrate end-to-end use.
