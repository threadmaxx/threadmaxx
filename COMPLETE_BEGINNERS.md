# COMPLETE_BEGINNERS.md

A guide to making a game like `examples/tou2d/` from absolutely nothing,
using the threadmaxx engine.

This guide assumes you can read basic C++ — you know what a function is,
what a `struct` is, what a `#include` does. You don't need to have ever
made a game. You don't need to know what an "ECS" is, or what a "tick" is,
or what a "command buffer" is. We'll cover all of it. We'll go slow, we'll
build things step by step, and by the end you'll have a playable 2D arena
shooter that you actually understand line by line.

The reference project we're building toward is `examples/tou2d/`. We
won't quite match its final polish — the real thing has 30+ source files
and 159 unit tests — but we'll get to a real, runnable game that
demonstrates every important idea.

---

## Table of contents

1. [What even is a game?](#1-what-even-is-a-game)
2. [What is an Entity Component System?](#2-what-is-an-entity-component-system)
3. [Setting up the project](#3-setting-up-the-project)
4. [Hello, threadmaxx — your first moving entity](#4-hello-threadmaxx--your-first-moving-entity)
5. [Adding input — moving the ship with arrow keys](#5-adding-input--moving-the-ship-with-arrow-keys)
6. [Gravity and drag — making the ship feel right](#6-gravity-and-drag--making-the-ship-feel-right)
7. [Rendering — seeing the ship](#7-rendering--seeing-the-ship)
8. [Terrain — the world the ship lives in](#8-terrain--the-world-the-ship-lives-in)
9. [Weapons — pew pew](#9-weapons--pew-pew)
10. [Collision and damage](#10-collision-and-damage)
11. [Bots — opponents for your ship](#11-bots--opponents-for-your-ship)
12. [HUD — telling the player what's happening](#12-hud--telling-the-player-whats-happening)
13. [Particles — making it look good](#13-particles--making-it-look-good)
14. [Polish — pause, replay, options](#14-polish--pause-replay-options)
15. [Where do you go from here?](#15-where-do-you-go-from-here)

---

## 1. What even is a game?

Let's start at the beginning. A game, from the computer's point of view, is
a **loop**. It runs forever (or until you quit). Each time through the
loop, three things happen:

1. **Read what the player did** — which keys are pressed, where the mouse
   is.
2. **Update the world** — move ships forward, fire bullets, check who got
   hit.
3. **Draw it** — paint everything on the screen.

Then it loops back and does it again. And again. Sixty times per second,
usually. Faster than your eye can track.

This loop has a name: the **game loop**. Every video game ever made has
some version of it.

```
forever:
    input  = read_keyboard()
    world  = update(world, input)
    screen = draw(world)
```

### Ticks vs frames

The world-update step (number 2 above) and the draw step (number 3) don't
have to happen at the same rate. In fact, in serious games, they usually
don't.

- A **tick** is one iteration of the world-update step. Ticks are
  typically **fixed-rate**: 60 ticks per second, period, no matter how
  fast or slow your computer is.
- A **frame** is one iteration of the draw step. Frames can run as fast as
  your monitor allows (60, 144, 240 per second).

Why split them? Two big reasons.

**Reason 1: predictability.** If the world updates at a fixed rate,
gravity always pulls down by the same amount per tick. A ship traveling
at 10 units per second will always cover 10 units in 1 second — on any
computer. This is called **determinism**, and it's the foundation of every
replay system, every multiplayer protocol, and every "save the game state
to disk and reload it later" feature.

**Reason 2: smoothness.** If your computer is fast enough to draw 144
frames per second but you only update the world 60 times per second, you
can draw the *same* world state at slightly different camera angles in
between, and it looks buttery smooth. (We won't go too deep on this
"interpolation" trick, but threadmaxx supports it. It's in `RenderFrame::alpha`.)

### Why threadmaxx is a "fixed-step" engine

threadmaxx commits hard to the fixed-step model. Its `Engine::step()`
function advances the world by exactly one tick, exactly `1/60` seconds of
simulated time. The renderer might draw more often than that, but the
world only changes at tick boundaries. This is the same model used by
*Quake*, *Counter-Strike*, *Factorio*, *Slay the Spire*, and approximately
every multiplayer or competitive game ever shipped.

### What you've learned in this section

- Games are loops: input → update → draw.
- Ticks (world updates) and frames (screen draws) can run at different rates.
- Fixed-step ticks give you determinism, which buys you replays, save
  files, and predictable multiplayer.

---

## 2. What is an Entity Component System?

The naive way to make a game is to write a `class Ship` with an
`update()` method. Then a `class Bullet` with its own `update()`. Then a
`class Enemy`. Then you call `ship.update()`, `bullet.update()`,
`enemy.update()` every tick.

That works! For about an hour. Then you hit problems.

- **Performance.** Every ship lives in its own little chunk of memory,
  scattered all over the heap. The CPU spends most of its time waiting for
  memory.
- **Parallelism.** You'd like to update all 1000 bullets at the same time
  on 8 CPU cores. But if `Bullet::update()` reads from the world and
  writes to it, the cores will trample each other.
- **Save / load.** You want to write the entire game state to disk. But
  every `class` has its own private members; you have to write `save()`
  and `load()` methods on every one of them. Boring and error-prone.

The **Entity Component System** (or **ECS**) is a different way to think
about it. It splits the world into three things:

### Entity

An entity is just a number — a unique ID. Nothing more. Entity 42 might be
a ship. Entity 43 might be a bullet. Entity 44 might be a tree. The ID
itself tells you nothing; it just lets you refer to a thing.

In threadmaxx an entity is represented by `EntityHandle`. It's basically
`(id, generation)` — the generation goes up every time the slot is
recycled, so an "old" handle to a destroyed entity won't accidentally
refer to whatever lives in that slot now.

### Component

A component is **data attached to an entity**. Pure data, no methods.

For example, a `Transform` component holds an entity's position and
rotation. A `Velocity` component holds how fast it's moving. A `Health`
component holds how much HP it has.

An entity can have any combination of components. A ship might have
`Transform + Velocity + Health + RenderTag`. A bullet might have
`Transform + Velocity + Bullet`. A tree might have `Transform + RenderTag`.

threadmaxx ships with a dozen built-in component types
(`include/threadmaxx/Components.hpp`). You can also add your own via
`UserComponent<T>` — game-side types the engine doesn't need to know
about.

### System

A system is **behavior applied to entities that have certain components**.

For example, `MovementSystem` reads every entity that has a `Transform`
AND a `Velocity`, and updates `Transform` based on `Velocity * dt`.
`GravitySystem` reads every entity that has a `Velocity` AND a `Transform`
that's not on the ground, and pushes `Velocity` downward.

Crucially, systems don't care what *kind* of entity they're operating on.
`MovementSystem` doesn't know about ships or bullets or trees. It just
sees "things with Transform and Velocity", and moves them. This makes it
trivial to add a new "thing that moves" — just create an entity with a
Transform and a Velocity, and it'll automatically be moved next tick.

### Why this is fast

The killer feature of ECS isn't elegance, it's **performance**.

threadmaxx (like most modern ECS engines) stores components in **flat
arrays**. All the `Transform` components for similar entities live in one
contiguous `std::vector<Transform>`. All the `Velocity` components live in
another contiguous `std::vector<Velocity>`.

When `MovementSystem` runs, it walks those arrays in straight-line
memory order. The CPU prefetches the next cache line while it's
processing the current one. You can update a million entities in a few
milliseconds on a single core. You can update ten million across eight
cores.

You also get parallelism almost for free. If `MovementSystem` only reads
`Velocity` and only writes `Transform`, and `GravitySystem` only reads
`Transform` and only writes `Velocity`, threadmaxx can run both at the
same time on different cores — they touch different data, so they can't
trample each other.

### What you've learned in this section

- Entities are IDs.
- Components are data attached to entities.
- Systems are functions that operate on entities with specific components.
- The pay-off is performance, parallelism, and easy save/load.

If this is your first ECS, that's fine. The next sections will make it
concrete by building one.

---

## 3. Setting up the project

Time to get our hands dirty.

### Get threadmaxx building first

Make sure the engine itself builds and tests cleanly. From the repo root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
cd build && ctest --output-on-failure
```

That should print "100% tests passed". If it doesn't, fix that before
going further — there's no point chasing your own bugs if the engine
itself isn't healthy on your machine.

### Make a new example directory

```sh
mkdir -p examples/mygame
cd examples/mygame
```

### The smallest possible CMakeLists.txt

Create `examples/mygame/CMakeLists.txt`:

```cmake
add_executable(mygame main.cpp)
target_link_libraries(mygame PRIVATE threadmaxx::threadmaxx)
```

That's it. Two lines. The first creates an executable from one source
file. The second links it against the threadmaxx engine library.

### Wire it into the build

Open the top-level `CMakeLists.txt` and find the block that adds the
other example subdirectories. Add yours:

```cmake
if (THREADMAXX_BUILD_EXAMPLES)
    add_subdirectory(examples/minimal)
    add_subdirectory(examples/boids)
    add_subdirectory(examples/vulkan_renderer)
    add_subdirectory(examples/rpg_demo)
    add_subdirectory(examples/tou2d)
    add_subdirectory(examples/mygame)        # <-- new
endif()
```

### What you've learned in this section

You can compile a program that links against threadmaxx. That's a
checkpoint. We haven't written any code yet.

---

## 4. Hello, threadmaxx — your first moving entity

Write `examples/mygame/main.cpp`:

```cpp
#include <threadmaxx/threadmaxx.hpp>
#include <cstdio>

using namespace threadmaxx;

class MyGame : public IGame {
public:
    void onSetup(Engine& engine, World&, CommandBuffer& seed) override {
        std::printf("[mygame] setup\n");

        // Spawn one entity at position (0,0,0) moving at 1 unit/sec to the right.
        seed.spawnBundle(bundle(
            Transform{},                              // position 0, rotation identity
            Velocity{{1.0f, 0.0f, 0.0f}, {0,0,0}}     // 1 unit/s in +X
        ));
    }
};

int main(int argc, char** argv) {
    int maxTicks = (argc > 1) ? std::atoi(argv[1]) : 60;

    Engine engine(Config{});
    MyGame game;
    if (!engine.initialize(game)) {
        std::printf("[mygame] initialize failed\n");
        return 1;
    }

    for (int t = 0; t < maxTicks; ++t) {
        engine.step();
    }

    engine.shutdown();
    std::printf("[mygame] done\n");
    return 0;
}
```

Build and run:

```sh
cd ../..        # back to repo root
cmake --build build -j
./build/examples/mygame/mygame 60
```

You should see:

```
[mygame] setup
[mygame] done
```

The program ran 60 ticks. We didn't tell it to print anything per tick,
so it looks like nothing happened. But trust me, an entity is in there,
moving.

### Let's verify the entity actually moved

Replace your `MyGame` class with:

```cpp
class MyGame : public IGame {
public:
    EntityHandle ship;

    void onSetup(Engine& engine, World& world, CommandBuffer& seed) override {
        ship = engine.reserveEntityHandle();
        seed.spawn(ship, Transform{}, Velocity{{1.0f, 0.0f, 0.0f}, {}});
    }

    void onPostStep(Engine&, World& world) override {
        if (auto* t = world.tryGetTransform(ship)) {
            std::printf("[tick %llu] ship pos = (%.2f, %.2f, %.2f)\n",
                        (unsigned long long)world.tick(),
                        t->position.x, t->position.y, t->position.z);
        }
    }
};
```

Run again with `./build/examples/mygame/mygame 5`. You should see:

```
[mygame] setup
[tick 1] ship pos = (0.02, 0.00, 0.00)
[tick 2] ship pos = (0.03, 0.00, 0.00)
[tick 3] ship pos = (0.05, 0.00, 0.00)
[tick 4] ship pos = (0.07, 0.00, 0.00)
[tick 5] ship pos = (0.08, 0.00, 0.00)
[mygame] done
```

Each tick, X went up by about 0.0167 (which is `1.0 unit/sec * (1/60) sec`).
The engine integrated velocity into position automatically because
`Velocity` is a built-in component that the engine knows how to use.

### Wait — what just happened?

Let's break it down.

**`Engine engine(Config{})`** creates the engine. `Config` is a settings
struct; the default values are sensible.

**`engine.initialize(game)`** wires up your game. It calls `game.onSetup()`
exactly once. This is your chance to spawn initial entities, register
systems, configure things.

**`engine.reserveEntityHandle()`** gets a unique entity ID up front, so
you can reference it later. The entity doesn't actually exist yet — it's
just been reserved.

**`seed.spawn(ship, ...)`** records "please create entity `ship` with
these components". This is a **command**, not an immediate action. We'll
explain why in the next section.

**`engine.step()`** advances the world by one tick. Internally it:

1. Calls `preStep` on every registered system (we haven't registered any).
2. Runs every registered system's `update` in parallel where possible.
3. Calls `postStep` on every registered system.
4. Calls `game.onPostStep()`.
5. Commits all queued commands (this is when the entity actually appears).
6. Builds a `RenderFrame` and (if you have a renderer) submits it.

**`world.tryGetTransform(ship)`** looks up the ship's Transform. Returns
`nullptr` if the entity doesn't exist (which could happen if it got
destroyed).

### The big idea: command buffers

When you wrote `seed.spawn(...)`, you didn't actually spawn an entity. You
recorded an *intent* to spawn one. The actual spawn happens during the
"commit" phase, after all the systems finish reading the world.

Why such weird ceremony?

Imagine two systems running in parallel, both deciding to spawn a bullet.
If they both tried to mutate the world directly, they'd race. One might
clobber the other. With command buffers, each system records its intents
into a private queue. After both finish, the engine applies the queues
one at a time, in a known order. No races.

This is **the** load-bearing invariant of threadmaxx. Workers never touch
the live world directly. They only emit commands.

### What you've learned in this section

- Created an `IGame` subclass.
- Spawned an entity with `Transform + Velocity`.
- Watched the engine integrate velocity into position automatically every
  tick.
- Met the command buffer — the queue-and-commit pattern that lets the
  engine parallelize freely.

---

## 5. Adding input — moving the ship with arrow keys

We can't read the keyboard yet — we haven't picked a windowing library.
For now, let's simulate input with a system that "presses" virtual keys
based on the tick count, so we can prove the input → movement pipeline
works.

### Your first ISystem

Add a new system. Define it above `MyGame`:

```cpp
struct PlayerInput {
    bool thrust = false;
    bool turnLeft = false;
    bool turnRight = false;
};
```

`PlayerInput` is just a struct. To attach it to entities, we register it
as a user component:

```cpp
class MyGame : public IGame {
public:
    EntityHandle ship;
    UserComponentId playerInputId;

    void onSetup(Engine& engine, World& world, CommandBuffer& seed) override {
        playerInputId = engine.registerUserComponent<PlayerInput>();

        ship = engine.reserveEntityHandle();
        seed.spawn(ship, Transform{}, Velocity{{}, {}});
        addUserComponent<PlayerInput>(seed, playerInputId, ship, PlayerInput{});
    }
    // ... onPostStep as before ...
};
```

Now we need a system that reads `PlayerInput` and modifies `Velocity` to
respond to "thrust". Define it above `MyGame`:

```cpp
class FakeInputSystem : public ISystem {
public:
    const char* name() const noexcept override { return "fake_input"; }

    void update(SystemContext& ctx) override {
        // Pretend the user is holding "thrust" on even ticks.
        const bool thrust = (ctx.tick() % 2) == 0;

        ctx.single([&](World& world, CommandBuffer& cb) {
            // Walk every entity that has PlayerInput, update its bits.
            // (In a real game you'd do this for each connected device.)
            // For this demo: there's only one entity.
            // (We'll skip the actual write for now and let ShipControlSystem read directly.)
        });
    }
};

class ShipControlSystem : public ISystem {
public:
    UserComponentId playerInputId;

    const char* name() const noexcept override { return "ship_control"; }
    ComponentSet reads()  const noexcept override { return Component::Transform | Component::Velocity; }
    ComponentSet writes() const noexcept override { return Component::Velocity; }

    void update(SystemContext& ctx) override {
        const auto dt = static_cast<float>(ctx.dt());
        // Pretend thrust input every other tick.
        const bool thrust = (ctx.tick() % 2) == 0;
        const float thrustForce = 2.0f;

        forEachChunk<Transform, Velocity>(ctx,
            [&](std::span<const EntityHandle> es,
                std::span<const Transform>    ts,
                std::span<const Velocity>     vs,
                CommandBuffer&                cb) {
                for (std::size_t i = 0; i < es.size(); ++i) {
                    Velocity next = vs[i];
                    if (thrust) {
                        next.linear.x += thrustForce * dt;
                    }
                    cb.setVelocity(es[i], next);
                }
            });
    }
};
```

OK, lots happening. Let's unpack.

### Anatomy of an ISystem

Every system implements three lifecycle methods (you only have to
override the ones you care about):

- **`preStep(SystemContext&)`** — runs first, serial, in registration
  order. Use for reading input, resetting per-tick scratch state.
- **`update(SystemContext&)`** — the main work. Runs in parallel where
  possible.
- **`postStep(SystemContext&)`** — runs last, serial, in registration
  order. Use for per-tick aggregates, event publishes.

And three optional declarations that help the scheduler:

- **`name()`** — for logging and stats.
- **`reads()`** — which components this system reads.
- **`writes()`** — which components this system writes.

`reads()` and `writes()` are how the engine decides which systems can run
at the same time. Two systems that don't conflict on components run on
different cores. Two systems where one writes what the other reads run
sequentially.

### `forEachChunk` — the workhorse query

`forEachChunk<Transform, Velocity>(ctx, callback)` does this:

1. Find every "archetype chunk" (a group of entities that all have the
   same set of components) that contains at least `Transform` AND
   `Velocity`.
2. For each chunk, call your callback with three `std::span`s: the entity
   handles, the Transforms, and the Velocities — all parallel arrays of
   the same length.
3. Plus a `CommandBuffer` you can use to record mutations.

You iterate the spans, do your math, and call `cb.setVelocity(handle,
newValue)` (or `cb.setTransform`, or whatever) to record the change. The
change applies during commit, after the system finishes.

### Wire up the systems

Add to `onSetup`:

```cpp
auto control = std::make_unique<ShipControlSystem>();
control->playerInputId = playerInputId;
engine.registerSystem(std::move(control));
```

Build, run, and watch the velocity actually change tick-by-tick. (We're
still just printing position; you'll see it accelerate now.)

### Real input

When you eventually wire up real input (GLFW, SDL, whatever), the pattern
is:

1. In your `main()`'s loop, poll the windowing library before each
   `engine.step()`.
2. Stash the keyboard state in a member of your `MyGame` class.
3. Write an `InputSystem` whose `preStep` reads that state and writes
   `PlayerInput` components for each player.

`examples/tou2d/InputSystem.cpp` does exactly this. It's a clean read.

### What you've learned in this section

- Defined a user component (`PlayerInput`).
- Wrote your first system (`ShipControlSystem`).
- Met `reads()` / `writes()` — how the engine plans the schedule.
- Met `forEachChunk` — the fast query for hot paths.
- Saw how mutations flow through `CommandBuffer` (not directly into the
  world).

---

## 6. Gravity and drag — making the ship feel right

Right now the ship just accelerates in +X forever. That's not very
ship-like. Let's add gravity (pulls things down) and drag (slows things
to a stop when no thrust is applied).

### A simple GravitySystem

```cpp
class GravitySystem : public ISystem {
public:
    float gravityAccel = 9.8f;     // units per second^2

    const char* name() const noexcept override { return "gravity"; }
    ComponentSet reads()  const noexcept override { return Component::Velocity; }
    ComponentSet writes() const noexcept override { return Component::Velocity; }

    void update(SystemContext& ctx) override {
        const auto dt = static_cast<float>(ctx.dt());
        const float deltaVy = -gravityAccel * dt;

        forEachChunk<Velocity>(ctx,
            [&](std::span<const EntityHandle> es,
                std::span<const Velocity>     vs,
                CommandBuffer&                cb) {
                for (std::size_t i = 0; i < es.size(); ++i) {
                    Velocity next = vs[i];
                    next.linear.y += deltaVy;
                    cb.setVelocity(es[i], next);
                }
            });
    }
};
```

Register it after `ShipControlSystem`:

```cpp
engine.registerSystem(std::make_unique<GravitySystem>());
```

Run and you'll see the ship start to fall (`pos.y` goes negative).

### Drag

Drag is "slow things down a bit each tick". The simplest version:

```cpp
// At the bottom of ShipControlSystem::update, after the thrust loop:
forEachChunk<Velocity>(ctx,
    [&](std::span<const EntityHandle> es,
        std::span<const Velocity>     vs,
        CommandBuffer&                cb) {
        const float dragRate = 0.5f;  // per second
        const float dragStep = std::exp(-dragRate * dt);
        for (std::size_t i = 0; i < es.size(); ++i) {
            Velocity next = vs[i];
            next.linear = next.linear * dragStep;
            cb.setVelocity(es[i], next);
        }
    });
```

Now the ship will accelerate under thrust, but if you stop thrusting it
gradually comes to rest. Real flight model.

### Why both `ShipControlSystem` and `GravitySystem` write `Velocity`

They both have `writes() == Velocity`. That means the engine won't run
them at the same time on different cores — they'd race. Instead it puts
them in different **waves**. The first wave runs `ShipControlSystem`; the
second wave runs `GravitySystem`. Each wave fully commits before the next
starts. Order is **registration order** when there's a conflict.

If you added a third system whose `writes()` didn't overlap with the
others (say, `RenderSystem` that only writes `RenderTag`), it'd be free
to share a wave with one of them.

You can see the actual wave assignment by calling
`engine.taskGraphSnapshot()` and inspecting the result. Most of the time
you don't need to — declaring reads/writes correctly is enough.

### What you've learned in this section

- Wrote `GravitySystem`, a system that affects every entity with Velocity.
- Saw how the wave scheduler handles two systems that both write Velocity
  (sequential).
- Added drag using `exp(-rate * dt)` — the classic "geometric decay"
  trick that frame-rate-independently slows things down.

---

## 7. Rendering — seeing the ship

So far we've been printing to the terminal. That's fine for debugging,
but it's not a game until you can see it.

### What threadmaxx gives you

The engine is renderer-agnostic. It builds a `RenderFrame` every tick — a
flat data structure containing:

- A list of **cameras**: where to look from, what projection to use.
- A list of **draw items** sorted into passes (Opaque, Transparent,
  ShadowCasters, Overlay).
- A list of **debug primitives** (`DebugLine`, `DebugPoint`, `DebugText`)
  — cheap UI you can paint without managing meshes.
- A flat **instances** list: every entity that has `RenderTag` and not
  `DisabledTag` gets one.

Your renderer takes that data and turns it into pixels. The simplest
renderer is `ConsoleRenderer` (in `examples/minimal/`) — it just prints a
summary. The fanciest is `threadmaxx_vk` (the Vulkan renderer used by
`rpg_demo` and `tou2d`).

For learning, we'll use ConsoleRenderer first.

### Use ConsoleRenderer

In your `main.cpp`:

```cpp
#include "../minimal/ConsoleRenderer.hpp"   // borrow it

// ... inside main, before engine.initialize:
ConsoleRenderer renderer;
engine.setRenderer(&renderer);
```

Re-run with a higher tick count. You'll see frame summaries.

### Adding RenderTag so the ship shows up

The engine auto-populates the `instances` list from entities that have
`RenderTag`. Add it to your spawn:

```cpp
seed.spawn(ship, Transform{}, Velocity{}, RenderTag{42});
// The 42 is meshId — for the console renderer it doesn't matter.
```

Now ConsoleRenderer reports one instance per frame.

### Stepping up to Vulkan

If you have Vulkan 1.3, GLFW, and `glslc` installed, you can use the
`threadmaxx_vk` renderer. It's documented in
`include/threadmaxx_vk/VulkanRenderer.hpp`. Cribbing from
`examples/rpg_demo/main.cpp` is the fastest path.

The key thing to understand: the renderer doesn't care what your game
*is*. It just consumes the `RenderFrame`. You could swap from Vulkan to
DirectX to a text-mode renderer and nothing about your systems would
change.

### Cameras

Every renderer needs at least one camera, otherwise it has no idea where
to draw from. You add cameras through a system's `buildRenderFrame` hook:

```cpp
class CameraSystem : public ISystem {
public:
    EntityHandle followTarget;

    const char* name() const noexcept override { return "camera"; }
    ComponentSet reads() const noexcept override { return Component::Transform; }

    void buildRenderFrame(RenderFrameBuilder& fb) override {
        // Read the follow target's position from the most recent world view.
        // (We'd need world access here in real code — for brevity, assume followPos is cached.)
        Vec3 followPos{0, 0, 0};  // ...
        Camera cam{};
        cam.mode = ProjectionMode::Orthographic;
        cam.viewport = {0, 0, 1, 1};  // full screen
        cam.position = followPos + Vec3{0, 0, 10};  // 10 units back
        cam.lookAt = followPos;
        // ... fill in projection matrix per Camera.hpp ...
        fb.addCamera(cam);
    }
};
```

`tou2d`'s `CameraSystem.cpp` is the reference implementation. It handles
multi-player split-screen too.

### What you've learned in this section

- The engine builds a `RenderFrame` every tick; renderers consume it.
- `RenderTag` opts an entity into the auto-instanced lane.
- `buildRenderFrame` is the per-system hook for emitting cameras, debug
  geometry, and hierarchical draw items.

---

## 8. Terrain — the world the ship lives in

Right now the ship floats in empty space. Let's give it some ground.

### Tiles, not entities

You might be tempted to make each terrain block its own entity. Don't.
A 500×500 tile map would be 250,000 entities, and you'd be paying
ECS-bookkeeping cost for each one.

Instead, store the tile grid in a plain `std::vector<uint8_t>`. One byte
per tile encoding "solid", "air", "water", "repair pad", etc. Keep it
outside the ECS entirely.

```cpp
enum class Attribute : uint8_t {
    Air        = 0,
    Solid      = 1,
    Repair     = 2,
    RepairBase = 3,
    Water      = 4,
};

class TerrainGrid {
public:
    void reset(int w, int h) {
        width_ = w;
        height_ = h;
        attrs_.assign(static_cast<size_t>(w * h), Attribute::Air);
        hp_.assign(static_cast<size_t>(w * h), 0);
    }

    Attribute attrAt(int cx, int cy) const {
        if (cx < 0 || cx >= width_ || cy < 0 || cy >= height_) return Attribute::Solid;
        return attrs_[cy * width_ + cx];
    }

    void setSolid(int cx, int cy, uint8_t hp) {
        attrs_[cy * width_ + cx] = Attribute::Solid;
        hp_[cy * width_ + cx] = hp;
    }

    // ... etc ...

private:
    int width_ = 0, height_ = 0;
    std::vector<Attribute> attrs_;
    std::vector<uint8_t> hp_;
};
```

### Collision

A `TerrainCollisionSystem` reads ship positions, looks up the tile at
each position, and bounces or stops the ship if it hits something solid.

```cpp
class TerrainCollisionSystem : public ISystem {
public:
    const TerrainGrid* grid = nullptr;
    float tileSize = 4.0f;  // world units per tile

    ComponentSet reads()  const noexcept override { return Component::Transform | Component::Velocity; }
    ComponentSet writes() const noexcept override { return Component::Transform | Component::Velocity; }

    void update(SystemContext& ctx) override {
        if (!grid) return;
        const auto dt = static_cast<float>(ctx.dt());

        forEachChunk<Transform, Velocity>(ctx,
            [&](std::span<const EntityHandle> es,
                std::span<const Transform>    ts,
                std::span<const Velocity>     vs,
                CommandBuffer&                cb) {
                for (std::size_t i = 0; i < es.size(); ++i) {
                    Vec3 nextPos = ts[i].position + vs[i].linear * dt;
                    int cx = (int)(nextPos.x / tileSize);
                    int cy = (int)(nextPos.y / tileSize);
                    if (grid->attrAt(cx, cy) == Attribute::Solid) {
                        // Stop the ship and don't let it enter the tile.
                        Velocity zero = vs[i];
                        zero.linear = Vec3{};
                        cb.setVelocity(es[i], zero);
                        // Don't apply the would-be position update.
                    }
                }
            });
    }
};
```

### Destructible terrain

Want bullets to chew through terrain? Have a system listen for
`BulletHit` events, look up the tile, decrement its HP, and clear it if
HP reaches zero. We'll cover events in §10.

### What you've learned in this section

- Terrain doesn't have to be entities. A flat byte grid is faster and
  simpler.
- A collision system reads positions, looks up tiles, and reacts.
- Destructible terrain is a tile-HP update triggered by events.

---

## 9. Weapons — pew pew

Let's let the ship fire bullets.

### A `Bullet` user component

```cpp
struct Bullet {
    EntityHandle owner;
    int ttlTicks = 120;     // 2 seconds at 60 Hz
    int damage = 10;
};
```

Register it in `onSetup`:

```cpp
bulletId = engine.registerUserComponent<Bullet>();
```

### A `WeaponFireSystem`

When the player presses fire, spawn a bullet.

```cpp
class WeaponFireSystem : public ISystem {
public:
    UserComponentId playerInputId;
    UserComponentId bulletId;

    ComponentSet reads()  const noexcept override { return Component::Transform | Component::Velocity; }

    void update(SystemContext& ctx) override {
        forEachChunk<Transform, Velocity>(ctx,
            [&](std::span<const EntityHandle> es,
                std::span<const Transform>    ts,
                std::span<const Velocity>     vs,
                CommandBuffer&                cb) {
                // For each ship, check input. Assume the input "fire" bit is set.
                bool fire = true;  // ... read from PlayerInput user component ...
                if (!fire) return;

                for (std::size_t i = 0; i < es.size(); ++i) {
                    // Spawn a bullet right in front of the ship, moving forward fast.
                    Vec3 muzzle = ts[i].position;  // could offset by orientation
                    Vec3 bv = vs[i].linear + Vec3{50.0f, 0, 0};  // launch speed

                    auto bulletEnt = ctx.reserveHandle();
                    cb.spawn(bulletEnt,
                             Transform{muzzle, ts[i].orientation},
                             Velocity{bv, {}},
                             RenderTag{99});
                    addUserComponent<Bullet>(cb, bulletId, bulletEnt,
                                             Bullet{es[i], 120, 10});
                }
            });
    }
};
```

### A `BulletLifetimeSystem`

Each tick, decrement bullet TTL and destroy expired bullets.

```cpp
class BulletLifetimeSystem : public ISystem {
public:
    UserComponentId bulletId;

    void update(SystemContext& ctx) override {
        ctx.single([&](World& world, CommandBuffer& cb) {
            // Walk every archetype chunk that has the Bullet user component.
            const auto& view = ctx.worldView();
            for (const auto* chunkPtr : view.chunks()) {
                if (!chunkPtr) continue;
                const auto& chunk = *chunkPtr;
                if (!chunk.mask.has(bulletId.componentBit())) continue;
                auto bullets = threadmaxx::user::chunkSpan<Bullet>(chunk, bulletId);
                for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                    // user::chunkSpan returns a const span; mutate via cb if needed.
                    Bullet next = bullets[row];
                    next.ttlTicks--;
                    if (next.ttlTicks <= 0) {
                        cb.destroy(chunk.entities[row]);
                    }
                    // (For "decrement and keep", see the addUserComponent/setUserData
                    // patterns in examples/tou2d/BotControlSystem.cpp.)
                }
            }
        });
    }
};
```

**Sidebar: built-in vs user-component iteration.**
`forEachChunk<T...>` and `forEachWith<T...>` only work on the built-in
component types (Transform, Velocity, Health, etc.). User components
need the explicit chunk walk above plus `user::chunkSpan<T>(chunk, id)`
to get the parallel array. The pattern is more verbose but it's mostly
boilerplate — `examples/tou2d/BotControlSystem.cpp` is a clean
reference. We'll use the explicit pattern for all user-component
iteration in this guide.

### Why destruction is a command too

Notice `cb.destroy(e)` — destroying an entity is a command, just like
spawning one. It's queued and applied during commit. You can't pull the
rug out from under another system that's still reading.

### What you've learned in this section

- Defined a `Bullet` user component.
- Spawned new entities (bullets) from inside a system.
- Used `ctx.reserveHandle()` to allocate an entity ID inside a job.
- Destroyed entities via the command buffer.

---

## 10. Collision and damage

Bullets that don't hit anything are boring.

### SpatialHash for broadphase

A naive "check every bullet against every ship" is O(N×M). With 100
bullets and 10 ships that's 1000 checks per tick — fine. With 10,000
bullets and 100 ships it's a million per tick — not fine.

threadmaxx ships a `SpatialHash<Payload>` in `SpatialHash.hpp`. Uniform
grid; insert in preStep, query in workers. Use it like this:

```cpp
class CollisionSystem : public ISystem {
public:
    SpatialHash<EntityHandle> shipHash{4.0f /*cell size*/};

    void preStep(SystemContext& ctx) override {
        // Rebuild every tick. clear() preserves capacity.
        shipHash.clear();
        ctx.single([&](World& world, CommandBuffer&) {
            // Walk every ship and insert it.
            // (Use a `Ship` user component as the filter.)
        });
    }

    void update(SystemContext& ctx) override {
        // For each bullet, query the hash for nearby ships.
        // Same chunk-walk pattern as BulletLifetimeSystem above —
        // bullets are a user component so forEachWith doesn't apply.
        const auto& view = ctx.worldView();
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(bulletId.componentBit())) continue;
            // ... iterate, query shipHash at bullet position, emit BulletHit on hit ...
        }
    }
};
```

### Events: `EventChannel<T>`

When the collision system finds a hit, it doesn't apply damage directly.
That'd couple the two systems too tightly. Instead it **emits an event**:

```cpp
struct BulletHit {
    EntityHandle bullet;
    EntityHandle ship;
    int damage;
};

// In CollisionSystem:
ctx.events<BulletHit>().emit(BulletHit{bulletEnt, shipEnt, b.damage});
```

A separate `DamageSystem` **subscribes** to those events:

```cpp
class DamageSystem : public ISystem {
public:
    void postStep(SystemContext& ctx) override {
        ctx.single([&](World& world, CommandBuffer& cb) {
            ctx.events<BulletHit>().drain([&](const BulletHit& hit) {
                if (auto* h = world.tryGetHealth(hit.ship)) {
                    Health next = *h;
                    next.current -= hit.damage;
                    cb.setHealth(hit.ship, next);
                    if (next.current <= 0) {
                        cb.addTag(hit.ship, Component::DestroyedTag);
                    }
                }
                // Bullet always dies on hit.
                cb.destroy(hit.bullet);
            });
        });
    }
};
```

### Why split it?

If `CollisionSystem` directly applied damage, *every* future damage source
would have to be wired into CollisionSystem. With events, anything can
emit `BulletHit` (or `MeleeHit`, or `ExplosionDamage`) and DamageSystem
handles them uniformly. Audio, particles, score tracking — they can all
subscribe too.

This is the same pattern Slack uses for chat messages, GitHub uses for
PRs, your operating system uses for window events. It's hard to
overstate how useful it is.

### Destruction follow-up

`DestroyedTag` is a marker; the engine doesn't auto-destroy. A
`ShipLifecycleSystem` reads it, plays a death animation, then either
respawns the ship or removes it.

### What you've learned in this section

- Used a `SpatialHash` for fast neighborhood queries.
- Met `EventChannel<T>` — typed, thread-safe, lock-free emit.
- Separated detection (collision) from response (damage) via events.
- Used `DestroyedTag` as a deferred "this needs to die" marker.

---

## 11. Bots — opponents for your ship

Bots are surprisingly easy in an ECS. They're just systems that produce
`PlayerInput` like a human does, but based on the world state.

### A simple bot

```cpp
struct BotAI {
    EntityHandle target;
    enum class State { Engage, Wander, Retreat } state = State::Wander;
};
```

```cpp
class BotControlSystem : public ISystem {
public:
    UserComponentId botId;
    UserComponentId playerInputId;

    ComponentSet reads() const noexcept override { return Component::Transform | Component::Health; }

    void update(SystemContext& ctx) override {
        ctx.single([&](World& world, CommandBuffer& cb) {
            const auto& view = ctx.worldView();
            for (const auto* chunkPtr : view.chunks()) {
                if (!chunkPtr) continue;
                const auto& chunk = *chunkPtr;
                if (!chunk.mask.has(botId.componentBit()))         continue;
                if (!chunk.mask.has(playerInputId.componentBit())) continue;

                auto bots    = threadmaxx::user::chunkSpan<BotAI>(chunk, botId);
                // PlayerInput we'd typically write via the command buffer too —
                // see examples/tou2d/BotControlSystem.cpp for the shape.

                for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                    EntityHandle e = chunk.entities[row];
                    auto* h = world.tryGetHealth(e);

                    BotAI next = bots[row];
                    if (h && h->current < h->max * 0.3f) {
                        next.state = BotAI::State::Retreat;
                    } else if (next.target.valid()) {
                        next.state = BotAI::State::Engage;
                    } else {
                        next.state = BotAI::State::Wander;
                    }

                    PlayerInput in{};
                    switch (next.state) {
                    case BotAI::State::Engage:  in.thrust = true; break;
                    case BotAI::State::Wander:  in.thrust = true; break;
                    case BotAI::State::Retreat: in.thrust = true; break;
                    }
                    // Write `in` to the slot's PlayerInput via your
                    // user-component setter (game-side helper). The real
                    // shape lives in BotControlSystem.cpp.
                }
            }
        });
    }
};
```

That's it. The bot just writes `PlayerInput` — exactly the same data
the human input writes. Downstream systems (`ShipControlSystem`,
`WeaponFireSystem`) don't know or care which entity is human and which
is bot.

### Determinism

The bot's "random turn" should use a seeded RNG, not `rand()`. Otherwise
two replay sessions diverge. The pattern is:

```cpp
uint32_t xorshift32(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}
```

Seed per-bot, store the seed in the `BotAI` component. Now every replay
on every machine produces identical bot behavior.

### What you've learned in this section

- Bots are systems that produce `PlayerInput`, just like humans.
- Downstream systems are bot-agnostic.
- Use seeded RNGs (not `rand()`) for replay determinism.

---

## 12. HUD — telling the player what's happening

The HUD (Heads-Up Display) is the score, the HP bar, the ammo indicator.
The cheapest way to draw a HUD in threadmaxx is **debug geometry**:
`DebugLine`, `DebugPoint`, `DebugText`. They're meant for diagnostic
overlays, but they're perfect for prototyping a HUD too.

```cpp
class HudSystem : public ISystem {
public:
    EntityHandle playerShip;

    void buildRenderFrame(RenderFrameBuilder& fb) override {
        // ... look up player's health ...
        float hpFrac = 0.7f;  // pretend

        // Draw an HP bar at the top of the screen using DebugLines.
        Vec3 barStart{-1, 0.9f, 0};
        Vec3 barEnd  {-1 + 2 * hpFrac, 0.9f, 0};
        uint32_t color = 0xFF00FF00;  // green
        fb.addDebugLine(DebugLine{barStart, barEnd, color, /*thickness*/ 4});

        // Score in the corner.
        fb.addDebugText(Vec3{0.8f, 0.9f, 0}, "Score: 1234", 0xFFFFFFFF);
    }
};
```

### Per-camera HUD with `cameraMask`

If you have split-screen, each viewport's HUD should only show in that
viewport. `DebugLine` and `DebugPoint` carry a `cameraMask` field — a
32-bit bitmask where bit `k` means "visible in `cameras[k]`". Setting
`cameraMask = (1u << slot)` confines a primitive to that slot's camera.

The default (all-ones) means "visible everywhere", which is what you
want for a global "Winner!" banner.

### What you've learned in this section

- Use debug geometry for fast HUD prototyping.
- Use `cameraMask` to confine HUD primitives to one viewport in
  split-screen.

---

## 13. Particles — making it look good

Particles are tiny entities that look pretty and then go away. Explosion
sparks, smoke, thruster plumes. They're how a competent-looking game
becomes a great-feeling game.

### A `Particle` user component

Keep it small. Particles spawn by the thousand; every byte matters.

```cpp
struct Particle {
    int16_t ttlTicks;        // remaining lifetime
    int16_t maxTtlTicks;     // for color/size interpolation
    uint32_t color;          // packed RGBA
};
```

Pair it with `Transform` and `Velocity` for movement. The engine's
built-in integration handles position updates for free.

### A `ParticleSystem`

```cpp
class ParticleSystem : public ISystem {
public:
    UserComponentId particleId;

    void update(SystemContext& ctx) override {
        ctx.single([&](World& world, CommandBuffer& cb) {
            const auto& view = ctx.worldView();
            for (const auto* chunkPtr : view.chunks()) {
                if (!chunkPtr) continue;
                const auto& chunk = *chunkPtr;
                if (!chunk.mask.has(particleId.componentBit())) continue;
                auto parts = threadmaxx::user::chunkSpan<Particle>(chunk, particleId);
                for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
                    if (parts[row].ttlTicks <= 0) {
                        cb.destroy(chunk.entities[row]);
                    }
                    // TTL decrement uses your setUserData-style helper;
                    // see ParticleSystem.cpp for the real shape.
                }
            }
        });
    }

    void buildRenderFrame(RenderFrameBuilder& fb) override {
        // Draw every particle as a DebugPoint with size + color from its TTL.
        // (Walk via worldView in a real implementation.)
    }
};
```

### Color over lifetime

Particles look much better when they change color as they age. The
thruster plume starts white-hot, fades to orange, then dark red, then
disappears. Implement with a lerp:

```cpp
uint32_t kHot  = 0xFFFFFFFFu;  // white
uint32_t kCool = 0xFF604040u;  // dark red

float t = float(p.maxTtlTicks - p.ttlTicks) / float(p.maxTtlTicks);
uint8_t r = lerp(getR(kHot), getR(kCool), t);
// ... and g, b, a ...
```

### Photosensitivity

If particles flash bright and fast, you can give players migraines or
trigger seizures. tou2d ships a "photosensitive mode" toggle that scales
particle alpha by 0.4. It's a one-line cap in `buildRenderFrame`. Do this
in your game. People will thank you.

### What you've learned in this section

- Particles are tiny entities with TTL.
- Keep their POD small (≤ 16 bytes ideal).
- Animate color over lifetime for the right feel.
- Add an accessibility toggle for photosensitive players.

---

## 14. Polish — pause, replay, options

Real games have pause menus, save files, options screens, and replay
playback. threadmaxx makes these surprisingly cheap.

### Pause

```cpp
engine.setPaused(true);
```

That's the entire API. `step()` becomes a no-op. Per-tick stats zero
out. The renderer keeps re-submitting the last frame so the screen
doesn't freeze.

Resume with `setPaused(false)`. The world picks up exactly where it left
off — no drift, no glitches.

### Replay

Replay in threadmaxx works on two pieces:

1. **Input log.** Record every `PlayerInput` per tick into a flat buffer.
2. **Commit hash stream.** `EngineStats::commitHash` is a 64-bit hash of
   the world state at the end of each tick. Record it too.

To play back:

1. Replace your `InputSystem` with one that reads from the log.
2. Run `engine.step()`.
3. Assert that the recorded `commitHash` matches what you recorded
   originally.

If the hashes ever diverge, you have a determinism bug. tou2d's
`Replay.cpp` does exactly this; the hash comparison catches every
non-determinism source on the first frame they appear.

### Options

Make a `Settings` struct, write it to disk as a POD, read it back at
startup. Host-endian is fine if you only support one platform.

```cpp
struct Settings {
    uint32_t magic = 0x474D5453;  // 'SMTG'
    uint32_t version = 1;
    float masterVolume = 1.0f;
    float musicVolume = 0.5f;
    uint8_t hudScale = 100;
    uint8_t photosensitive = 0;
    // ... etc ...
};

void saveSettings(const Settings& s, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(&s), sizeof(s));
}

bool loadSettings(Settings& s, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.read(reinterpret_cast<char*>(&s), sizeof(s));
    return f && s.magic == 0x474D5453 && s.version == 1;
}
```

If the file's missing or the magic doesn't match, fall back to defaults.
Don't crash. Players don't read crash logs.

### What you've learned in this section

- Pause is `setPaused(true)`. That's it.
- Replay = record input + record commit hash + replay = assert hash
  matches.
- Settings persistence is a POD on disk. Magic + version up front; sane
  fallback when invalid.

---

## 15. Where do you go from here?

You now have a game. Maybe not a polished one, but a *real* one — with
ships, bullets, terrain, bots, particles, a HUD, pause, and replay. You
understand every line.

Here's what to read next, in rough order of usefulness:

### Read the real tou2d source

Open `examples/tou2d/` and start reading. Every system you wrote in this
guide has a real counterpart there — bigger, more careful, with more
features, but the same shape. `MovementSystem.cpp` is movement. The
`HudSystem.cpp` is the HUD. The `ParticleSystem.cpp` is particles. You
just wrote miniature versions of all of them.

Specifically:

- `TouGame.cpp` — `IGame` implementation. See how it wires up everything.
- `BotControlSystem.cpp` — the real bot AI. Fancier than ours, but the
  same pattern: read state, decide action, write `PlayerInput`.
- `ParticleSystem.cpp` — the real particle system. Watch how it uses
  `forEachChunk` for the hot path.
- `Replay.cpp` — real input-log + commit-hash replay.

### Read the engine playbooks

- `ARCHITECTURE.md` — the engine's design overview. Read it once front to
  back when you're ready to think about *why* threadmaxx is shaped this
  way.
- `CLAUDE.md` — the contributor playbook. Lists every invariant, every
  load-bearing detail. Reference material; grep when you need it.
- `doc/index.md` — the user guide. More structured than CLAUDE.md, less
  reference-y. Good for "I want to understand topic X."

### Read the engine code

- `include/threadmaxx/Engine.hpp` — the public surface. Every method is
  documented.
- `include/threadmaxx/CommandBuffer.hpp` — every spawn / set / destroy
  primitive.
- `include/threadmaxx/Query.hpp` — `forEachChunk`, `forEachWith`,
  `MaskCache`. Reference for "what queries exist and when to use which."
- `include/threadmaxx/render/` — the renderer-neutral PODs.

### Experiment

Pick a feature you wish your game had and build it. A power-up that
doubles fire rate for 5 seconds. A second weapon type. A leaderboard.
Each one is one new component + one new system. The pattern doesn't
change.

The hardest part of game programming is finishing. You've gotten past
the conceptual cliff. Everything from here is iteration.

### Don't forget the test

Every system you write should ship with a test. The engine's test suite
(`tests/` + `examples/tou2d/tou2d_*_test.cpp`) is the contract. If you
have a `BotControlSystem`, write a `bot_test.cpp` that pins its key
behaviors. Tests are the difference between a game you can refactor next
week and one you're afraid to touch.

---

## A note from the author

Writing your first game is hard. There's a lot to keep in your head at
once: input, simulation, rendering, audio, save data, networking,
accessibility, polish.

The good news: every one of those is *just code*. Not magic. You read
some headers, you write some functions, you check that they do what you
think. The patterns in this guide work. The engine handles the scary
parallel-execution and cache-locality stuff so you can stay focused on
the design.

If you get stuck, look at how the real tou2d solves the same problem.
If you get *really* stuck, the bug is almost always one of three things:

1. **You forgot to register a system.** Check `engine.registerSystem(...)`
   is called for every system you wrote.
2. **You're writing directly instead of through `CommandBuffer`.** Look
   for any `world.something = ...` outside an `IGame` callback. Those
   are bugs.
3. **You declared `reads()` / `writes()` wrong.** Two systems that both
   write the same component need to be in different waves; the scheduler
   does this for you, but only if you tell the truth in `reads()` and
   `writes()`.

That's most of it. Go make something cool.
