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

There is no test suite yet. The example program (`examples/minimal`) is the integration smoke test: a successful run prints `[frame]` lines with monotonically increasing ticks and entity counts and ends with `[ConsoleRenderer] shutdown after N frames`.

Always pass `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` on first configure — without it, clangd can't see the include paths or `cxx_std_20` and will report spurious errors on every header.

## Architecture invariants

`ARCHITECTURE.md` has the design overview and threading diagram — read it before structural changes. The invariants below are load-bearing; violating them silently corrupts state under load.

**Worker jobs never mutate live world state.** They receive `const World&` and a private `CommandBuffer`. All mutation flows through `EngineImpl::commitBuffer()` which runs single-threaded on the simulation thread, after a batch's `std::latch` has fired. If you add a new path that writes to `EntityStorage` outside of `commitBuffer`, you've broken determinism and introduced a data race.

**Commit order is submission order, not execution order.** `JobSystem` workers race freely; the per-system `std::vector<CommandBuffer>` in `SystemContextImpl` is what's authoritative. `parallelFor` resizes that vector up front so pointers into it stay valid while jobs run — don't switch it to `emplace_back` inside the submit loop.

**The public surface (`include/threadmaxx/`) is the contract.** Everything in `src/` is PImpl'd behind `Engine`/`World` and can change freely. Resist adding new public headers; prefer extending existing ones (e.g. new accessor on `World`) over exposing internals. `EngineImpl` and `WorldImpl` are reachable from `World::impl_()` for engine-internal use only.

## Adding a new built-in component

This is the one operation that crosses every layer. To add component `Foo`:

1. Define the POD in `include/threadmaxx/Components.hpp`.
2. Add a parallel `std::vector<Foo>` to `EntityStorage` (storage, dense view accessor, `mutFoo`, and the swap-and-pop branch in `destroy()` and the push in `spawn()`).
3. Extend `spawn()` and `CmdSpawn` (or add `CmdSetFoo`) in `CommandBuffer.hpp`, then the matching method in `CommandBuffer.cpp`.
4. Handle the new variant alternative in `EngineImpl::commitBuffer` (the `std::visit` lambda).
5. Add the read accessor + dense span on `World` (`tryGetFoo`, `foos()`).
6. If it affects rendering, populate `RenderInstance` from it in `EngineImpl::buildRenderFrame`.

Missing any one of these will compile but corrupt state at runtime (dense arrays go out of sync). The `EntityStorage::destroy` swap-and-pop is the bit that tends to be forgotten.

## Render frame lifetime

`EngineImpl::buildRenderFrame()` writes into the back of a double-buffered pair (`renderInstanceBuffers_[0/1]` and `renderFrames_[0/1]`) and publishes via `frontIndex_.store(back, release)`. The `RenderFrame::instances` span points into the engine-owned vector — renderers must finish using it before `submitFrame` returns or copy what they need. Today `submitFrame` is called synchronously from the sim thread immediately after the swap, so single-threaded renderers don't need to worry; if a future renderer reads from another thread, the atomic swap is the synchronization point.
