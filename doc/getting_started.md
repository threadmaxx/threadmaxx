# Getting Started

@page getting_started Getting Started

This page takes you from a fresh checkout to a running simulation in about
five minutes.

## Prerequisites

- A C++20 compiler. Tested with GCC 16.1 and recent Clang. MSVC builds in
  CI under `/permissive-`.
- CMake ≥ 3.20.
- A threading-capable libc (linked via `Threads::Threads`).

No third-party dependencies. The SDL2 example is built only when CMake
detects SDL2 — without it, the rest of the project still builds.

## Build the library and run the headless example

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
./build/examples/minimal/threadmaxx_minimal 600    # 600 ticks then exit
```

You should see `[frame]` lines every ~60 ticks with a growing entity count,
ending with `[ConsoleRenderer] shutdown after 601 frames`.

The bounded form (`threadmaxx_minimal <N>`) drives `engine.step()` itself
and exits after `N` ticks. With no argument the program calls
`engine.run()` and waits for Ctrl-C.

## Run the test suite

```sh
cd build && ctest --output-on-failure
```

The suite (14 binaries at last count) is fast — under a second on a modern
laptop — and pins the documented invariants. New behavior should land with
a test in the same change.

## Minimum viable game

Below is a complete program that simulates one moving entity. It uses
every public concept you need to know:

```cpp
#include <threadmaxx/threadmaxx.hpp>

class MovementSystem : public threadmaxx::ISystem {
public:
    const char* name() const noexcept override { return "movement"; }

    // Declare which component categories we touch so the engine can run us
    // in parallel with non-conflicting systems.
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::Component::Transform | threadmaxx::Component::Velocity;
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::Component::Transform;
    }

    void update(threadmaxx::SystemContext& ctx) override {
        const auto dt = static_cast<float>(ctx.dt());
        threadmaxx::forEach<threadmaxx::Transform, threadmaxx::Velocity>(ctx,
            [dt](threadmaxx::EntityHandle e,
                 const threadmaxx::Transform& t,
                 const threadmaxx::Velocity& v,
                 threadmaxx::CommandBuffer& cb) {
                threadmaxx::Transform next = t;
                next.position = t.position + v.linear * dt;
                cb.setTransform(e, next);
            });
    }
};

class MyGame : public threadmaxx::IGame {
public:
    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        engine.registerSystem(std::make_unique<MovementSystem>());
        seed.spawn(threadmaxx::Transform{},
                   threadmaxx::Velocity{{1.0f, 0.0f, 0.0f}, {}});
    }
};

int main() {
    threadmaxx::Engine engine(threadmaxx::Config{});
    MyGame game;
    if (!engine.initialize(game)) return 1;
    engine.run();         // blocks until requestQuit()
    engine.shutdown();
}
```

Things to notice:

- The game owns nothing the engine doesn't already give back to it via
  `World&` and `CommandBuffer&`. There is no global state.
- The system never writes directly to entity storage. It reads dense
  spans and records mutations into the `CommandBuffer` it was handed. The
  engine commits buffers in submission order on the simulation thread.
- The renderer is optional. With `setRenderer` left unset the engine runs
  headless, which is useful for tests, dedicated servers, and CI.

## Linking against threadmaxx

In your own project:

```cmake
add_subdirectory(threadmaxx)
target_link_libraries(my_game PRIVATE threadmaxx::threadmaxx)
```

There is no install target yet; vendoring via `add_subdirectory` (or
`FetchContent`) is the supported path while the public API is still being
hardened.

## Build options

| Option | Default | What it does |
| --- | --- | --- |
| `THREADMAXX_BUILD_EXAMPLES` | `ON` | Builds `examples/minimal` and (if SDL2 is found) `examples/boids`. |
| `THREADMAXX_BUILD_TESTS` | `ON` | Builds and registers the CTest suite. |
| `THREADMAXX_WARNINGS_AS_ERRORS` | `OFF` | Promotes the project's warning set (incl. `-Wsign-conversion`, `-Wconversion`, `-Wshadow`, `-Wold-style-cast`) to errors. The library compiles clean under it — keep it that way. |

## Generate the API reference

If Doxygen is installed, the CMake configure step registers a `doc`
target:

```sh
cmake --build build --target doc
# Output: doc/generated/html/index.html
```

The Doxyfile pulls in this user guide, the headers, the architecture
document, and the future-work plan — so the generated site is a single
browsable entry point.

## Next step

Read [Core concepts](concepts.md). It is the shortest page in this guide
and the one that unlocks everything else.
