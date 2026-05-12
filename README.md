# threadmaxx

A renderer-agnostic C++20 game backend with a fixed-step simulation loop and a
worker-thread job system. Game code registers systems (movement, physics, AI,
...) that read the world in parallel and emit commands; the engine commits
them deterministically and hands a flat `RenderFrame` to whatever renderer
you plug in.

Status: early. The public API is small and intentionally minimal; the
internals are PImpl'd so they can change.

## Requirements

- C++20 (`std::span`, `std::latch`)
- CMake ≥ 3.20
- A threading-capable libc (linked via `Threads::Threads`)

Tested with GCC 16.1 on Linux. No third-party dependencies.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Run the bundled example:

```sh
./build/examples/minimal/threadmaxx_minimal       # Ctrl-C to quit
./build/examples/minimal/threadmaxx_minimal 600   # run exactly 600 ticks
```

Expected output: `[frame]` lines printed every ~60 ticks, instance counts
growing as the spawner system fires, clean shutdown.

CMake options:

| Option | Default | What it does |
| --- | --- | --- |
| `THREADMAXX_BUILD_EXAMPLES` | `ON` | Builds `examples/minimal`. |
| `THREADMAXX_WARNINGS_AS_ERRORS` | `OFF` | Promotes the project's warning set to errors. |

## Using it

Link against the `threadmaxx::threadmaxx` target:

```cmake
add_subdirectory(threadmaxx)
target_link_libraries(my_game PRIVATE threadmaxx::threadmaxx)
```

A minimal game looks like this:

```cpp
#include <threadmaxx/threadmaxx.hpp>

class MovementSystem : public threadmaxx::ISystem {
    const char* name() const noexcept override { return "movement"; }
    void update(threadmaxx::SystemContext& ctx) override {
        const auto entities   = ctx.world().entities();
        const auto transforms = ctx.world().transforms();
        const auto velocities = ctx.world().velocities();
        const auto dt         = static_cast<float>(ctx.dt());
        ctx.parallelFor(static_cast<std::uint32_t>(entities.size()), /*grain*/ 0,
            [=](threadmaxx::Range r, threadmaxx::CommandBuffer& cb) {
                for (auto i = r.begin; i < r.end; ++i) {
                    threadmaxx::Transform t = transforms[i];
                    t.position = t.position + velocities[i].linear * dt;
                    cb.setTransform(entities[i], t);
                }
            });
    }
};

class MyGame : public threadmaxx::IGame {
    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        engine.registerSystem(std::make_unique<MovementSystem>());
        // engine.setRenderer(&myRenderer);  // optional; null = headless
        seed.spawn(threadmaxx::Transform{}, threadmaxx::Velocity{{1, 0, 0}});
    }
};

int main() {
    threadmaxx::Engine engine(threadmaxx::Config{});
    MyGame game;
    engine.initialize(game);
    engine.run();          // blocks until requestQuit()
    engine.shutdown();
}
```

To plug in a renderer, implement `threadmaxx::IRenderer::submitFrame` — you
get a `RenderFrame` with a `std::span<const RenderInstance>`. See
`examples/minimal/ConsoleRenderer.{hpp,cpp}` for the smallest possible
implementation.

## Architecture, briefly

```
sim thread:  ┌─ for each system:
             │     ctx.parallelFor → JobSystem dispatches to workers
             │                         (each worker gets const World + own CommandBuffer)
             │     std::latch waits
             │     commit buffers in submission order  ← deterministic
             └─ build RenderFrame → atomic publish → renderer.submitFrame
```

Key properties:

- Workers never mutate live world state; they emit commands into per-job
  `CommandBuffer`s. All mutations are applied on the sim thread, single-
  threaded, in submission order — so the same inputs produce the same world.
- The renderer sees only the published `RenderFrame`; the engine owns the
  storage and double-buffers it.
- Gameplay code does not touch a mutex.

Full design + threading diagram: [`ARCHITECTURE.md`](ARCHITECTURE.md).

## Repository layout

```
include/threadmaxx/    public API (11 headers)
src/                   private implementation (PImpl)
examples/minimal/      runnable headless example
CMakeLists.txt
ARCHITECTURE.md        design overview + threading diagram
CLAUDE.md              guidance for Claude Code sessions in this repo
```
