# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build, run, iterate

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
./build/examples/minimal/threadmaxx_minimal          # Ctrl-C to quit
./build/examples/minimal/threadmaxx_minimal 600      # bounded: run exactly 600 ticks then exit
```

Useful options when configuring:

- `-DTHREADMAXX_WARNINGS_AS_ERRORS=ON` — promotes the GCC/Clang warning set (incl. `-Wsign-conversion`, `-Wconversion`, `-Wshadow`, `-Wold-style-cast`) to errors. The project compiles clean under it; keep it that way for new code.
- `-DTHREADMAXX_BUILD_EXAMPLES=OFF` — library only.
- `-DTHREADMAXX_BUILD_TESTS=OFF` — skip the test suite.

The test suite lives in `tests/` and runs through CTest: `cd build && ctest --output-on-failure`. It uses a tiny no-dependency harness (`tests/Check.hpp`) — one executable per test, non-zero exit means failure. New behavior should land with a test in the same PR; the suite is the contract for the invariants below.

The example program (`examples/minimal`) is the integration smoke test: a successful run prints `[frame]` lines with monotonically increasing ticks and entity counts and ends with `[ConsoleRenderer] shutdown after N frames`.

Always pass `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` on first configure — without it, clangd can't see the include paths or `cxx_std_20` and will report spurious errors on every header.

## Architecture invariants

`ARCHITECTURE.md` has the design overview and threading diagram — read it before structural changes. The invariants below are load-bearing; violating them silently corrupts state under load.

**Worker jobs never mutate live world state.** They receive `const World&` and a private `CommandBuffer`. All mutation flows through `EngineImpl::commitBuffer()` which runs single-threaded on the simulation thread, after a batch's `std::latch` has fired. If you add a new path that writes to `EntityStorage` outside of `commitBuffer`, you've broken determinism and introduced a data race.

**Commit order is submission order, not execution order.** `JobSystem` workers race freely; the per-system `std::vector<CommandBuffer>` in `SystemContextImpl` is what's authoritative. `parallelFor` resizes that vector up front so pointers into it stay valid while jobs run — don't switch it to `emplace_back` inside the submit loop.

**The public surface (`include/threadmaxx/`) is the contract.** Everything in `src/` is PImpl'd behind `Engine`/`World` and can change freely. Resist adding new public headers; prefer extending existing ones (e.g. new accessor on `World`) over exposing internals. `EngineImpl` and `WorldImpl` are reachable from `World::impl_()` for engine-internal use only.

**Systems declare reads/writes; the engine groups them into waves.** `ISystem::reads()` and `ISystem::writes()` return `ComponentSet`s. The defaults are `ComponentSet::all()`, which forces strict registration-order sequential execution. Overriding them lets non-conflicting systems share a wave and run concurrently. The conflict rule is `W∩W ∨ W∩R ∨ R∩W`. Waves are recomputed in `registerSystem`. A new built-in component must add a corresponding `Component::Foo` enum value (and update `ComponentSet::all()`'s mask) — otherwise systems writing it would alias another category in the scheduler. The single-threaded commit phase is preserved: same-wave systems write into their own `SystemContext` buffers, and commits happen in registration order after the wave finishes.

## Adding a new built-in component

This is the one operation that crosses every layer. To add component `Foo`:

1. Define the POD in `include/threadmaxx/Components.hpp`.
2. Add a `Component::Foo` enum value and extend `ComponentSet::all()`'s mask to cover its bit.
3. Add a parallel `std::vector<Foo>` to `EntityStorage` (storage, dense view accessor, `mutFoo`, and the swap-and-pop branch in `destroy()` and the push in `spawn()`). `EntityStorage::spawn` now also takes a `ComponentSet initialMask` — pass it through.
4. Extend `CmdSpawn` (add a `Foo foo` field), add `CmdSetFoo`, both `spawn` overloads, and the matching method in `CommandBuffer.cpp`. Update `defaultSpawnMask` if presence should auto-derive from values (`RenderTag` does this from `meshId >= 0`).
5. Handle the new variant alternatives in `EngineImpl::commitBuffer` (the `std::visit` lambda). For setters that change presence (`RenderTag`, `Parent`), also call `mutComponentMask` to add/remove the bit.
6. Add the read accessor + dense span on `World` (`tryGetFoo`, `foos()`).
7. If it affects rendering, populate `RenderInstance` from it in `EngineImpl::buildRenderFrame` (and gate on the new presence bit instead of a sentinel).
8. Optionally extend `Query.hpp::detail::getSpan` and `componentBit` so `forEach<Foo>` and `forEachWith<Foo>` work.

Missing any one of these will compile but corrupt state at runtime (dense arrays go out of sync). The `EntityStorage::destroy` swap-and-pop is the bit that tends to be forgotten. `Parent` (added 2026-05-13) and `Acceleration` show the full recipe end-to-end.

## Render frame lifetime

`EngineImpl::buildRenderFrame()` writes into the back of a double-buffered pair (`renderInstanceBuffers_[0/1]` and `renderFrames_[0/1]`) and publishes via `frontIndex_.store(back, release)`. The `RenderFrame::instances` span points into the engine-owned vector — renderers must finish using it before `submitFrame` returns or copy what they need. Today `submitFrame` is called synchronously from the sim thread immediately after the swap, so single-threaded renderers don't need to worry; if a future renderer reads from another thread, the atomic swap is the synchronization point.

`RenderFrame::alpha` is the wall-clock fraction (0..1) past the last committed tick. `step()` always submits the post-tick frame with `alpha=0`. In `run()`, after the inner step loop, the engine additionally calls `submitInterpolatedFrame(alpha)` which mutates `alpha` on the current front frame and re-submits — there's no rebuild, since world state is unchanged between ticks. Bypassing this and re-running `buildRenderFrame()` per interp submit would be wasted work; just don't.
