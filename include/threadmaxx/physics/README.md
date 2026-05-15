# `physics` — physics integration sibling library

## 1. Purpose

`physics` provides physics-facing runtime glue for games built on `threadmaxx`.

It is for:

* rigid body integration,
* broadphase / narrowphase orchestration,
* character controllers,
* collision queries,
* kinematic movement,
* constraint setup,
* physics-to-animation and physics-to-gameplay data flow,
* deterministic replay hooks.

It is **not** for:

* owning ECS storage,
* replacing the engine’s simulation loop,
* rendering,
* network replication,
* navmesh/pathfinding,
* animation blending,
* editor UI,
* hardcoding one solver’s world ownership model.

That matches the roadmap’s boundary: physics belongs above the engine, and the solver itself is not part of `threadmaxx`. 

## 2. Design principles

1. **Above the engine.** The core engine only supplies the hooks this library needs.
2. **Backend-agnostic solver adapter.** Jolt, Bullet, PhysX, or a custom solver can sit behind the same API.
3. **No world ownership in the engine.** Physics owns its own simulation world; `threadmaxx` just supplies entity/component data.
4. **Chunk-friendly inputs.** Batch updates should work over contiguous spans from archetype chunks.
5. **Read-only engine world snapshot.** Physics reads game state from snapshots, not from live mutable storage.
6. **Deterministic-friendly.** Same inputs, same tick, same outputs where the solver allows it.
7. **No allocations in hot paths.** Query and step code should be buffer- and span-based.
8. **Explicit sync points.** Physics steps, substeps, and commit-back phases must be obvious.
9. **Minimal public surface.** Keep the API small and practical.
10. **Game-code owns policy.** Collision layers, stepping mode, CCD, sleeping, and filter logic stay configurable.

## 3. Package layout

```text id="p6k4ra"
include/threadmaxx/physics/
  physics.hpp           // umbrella include
  config.hpp            // solver options, stepping, tolerances
  types.hpp             // PhysicsWorldId, BodyId, ShapeId, JointId
  body.hpp              // rigid body descriptors and state
  shape.hpp             // collider shapes
  query.hpp             // raycast, sweep, overlap, contact queries
  step.hpp              // simulation step interface
  sync.hpp              // engine ↔ physics synchronization helpers
  constraints.hpp       // joints and constraint definitions
  character.hpp         // character controller helpers
  contact.hpp           // contacts, manifolds, events
  debug.hpp             // debug-draw and diagnostics data
  serialization.hpp     // save/load for physics scene state
  backend.hpp           // solver backend interface
  detail/
    broadphase.hpp
    filter.hpp
    island.hpp
    motion.hpp
    cache.hpp
```

If you want solver integrations separated, put them under `src/physics/backends/`.

## 4. Core data model

### 4.1 Physics world and bodies

The engine already has the `PhysicsBodyRef` hook. This library supplies the runtime object behind it. 

```cpp id="b8m1qv"
namespace threadmaxx::physics {

struct PhysicsWorldId {
    std::uint64_t value{};
};

struct BodyId {
    std::uint64_t value{};
};

struct ShapeId {
    std::uint64_t value{};
};

enum class BodyType : std::uint8_t {
    Static,
    Dynamic,
    Kinematic
};

struct BodyDesc {
    BodyType type{BodyType::Dynamic};
    Vec3 position{};
    Quat rotation{};
    Vec3 linearVelocity{};
    Vec3 angularVelocity{};
    float mass{1.0f};
    float friction{0.5f};
    float restitution{0.0f};
    bool enableCCD{false};
    bool canSleep{true};
};

struct BodyState {
    Vec3 position{};
    Quat rotation{};
    Vec3 linearVelocity{};
    Vec3 angularVelocity{};
};

} // namespace threadmaxx::physics
```

### 4.2 Shapes

```cpp id="v0h7az"
namespace threadmaxx::physics {

enum class ShapeType : std::uint8_t {
    Box,
    Sphere,
    Capsule,
    ConvexHull,
    Mesh,
    Compound
};

struct ShapeDesc {
    ShapeType type{};
    Vec3 halfExtents{};
    float radius{};
    float height{};
    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> indices;
};

} // namespace threadmaxx::physics
```

### 4.3 Constraints

```cpp id="q1d3nx"
namespace threadmaxx::physics {

enum class ConstraintType : std::uint8_t {
    Fixed,
    Hinge,
    Slider,
    BallSocket,
    SixDof
};

struct ConstraintDesc {
    ConstraintType type{};
    BodyId bodyA{};
    BodyId bodyB{};
    Mat4 frameA{};
    Mat4 frameB{};
    bool disableCollisionBetweenLinkedBodies{true};
};

} // namespace threadmaxx::physics
```

## 5. Public API

### 5.1 Backend interface

This is the key adapter point.

```cpp id="w3f9le"
namespace threadmaxx::physics {

class IPhysicsBackend {
public:
    virtual ~IPhysicsBackend() = default;

    virtual PhysicsWorldId createWorld() = 0;
    virtual void destroyWorld(PhysicsWorldId world) = 0;

    virtual ShapeId createShape(const ShapeDesc& desc) = 0;
    virtual void destroyShape(ShapeId shape) = 0;

    virtual BodyId createBody(PhysicsWorldId world, const BodyDesc& desc,
                              std::span<const ShapeId> shapes) = 0;
    virtual void destroyBody(PhysicsWorldId world, BodyId body) = 0;

    virtual void stepWorld(PhysicsWorldId world, float dt) = 0;

    virtual void syncBodiesToGame(PhysicsWorldId world,
                                  std::span<BodyId> bodies,
                                  std::span<BodyState> outStates) = 0;
};

} // namespace threadmaxx::physics
```

This keeps the solver private and swappable.

### 5.2 Physics scene

```cpp id="t8c2vd"
namespace threadmaxx::physics {

class PhysicsScene {
public:
    explicit PhysicsScene(std::unique_ptr<IPhysicsBackend> backend);

    PhysicsWorldId createWorld();
    void destroyWorld(PhysicsWorldId world);

    ShapeId addShape(const ShapeDesc& shape);
    void removeShape(ShapeId shape);

    BodyId addBody(PhysicsWorldId world, const BodyDesc& desc,
                   std::span<const ShapeId> shapes);
    void removeBody(PhysicsWorldId world, BodyId body);

    void step(PhysicsWorldId world, float dt);

    std::optional<BodyState> bodyState(PhysicsWorldId world, BodyId body) const;
};

} // namespace threadmaxx::physics
```

### 5.3 Queries

```cpp id="n2g4sq"
namespace threadmaxx::physics {

struct RaycastRequest {
    PhysicsWorldId world{};
    Vec3 origin{};
    Vec3 direction{};
    float maxDistance{};
    std::uint32_t layerMask{0xFFFFFFFFu};
};

struct RaycastHit {
    bool hit{};
    Vec3 position{};
    Vec3 normal{};
    float distance{};
    BodyId body{};
};

std::optional<RaycastHit> raycast(const PhysicsScene& scene,
                                  const RaycastRequest& request);

} // namespace threadmaxx::physics
```

Add sweeps and overlaps in the same style:

```cpp id="a2v9rn"
namespace threadmaxx::physics {

struct SweepRequest {
    PhysicsWorldId world{};
    ShapeId shape{};
    Vec3 origin{};
    Quat rotation{};
    Vec3 direction{};
    float distance{};
    std::uint32_t layerMask{0xFFFFFFFFu};
};

std::vector<RaycastHit> overlap(const PhysicsScene& scene,
                                PhysicsWorldId world,
                                ShapeId shape,
                                Vec3 position,
                                Quat rotation,
                                std::uint32_t layerMask);

} // namespace threadmaxx::physics
```

### 5.4 Character controller

```cpp id="h7p8ku"
namespace threadmaxx::physics {

struct CharacterControllerDesc {
    float height{};
    float radius{};
    float stepHeight{};
    float slopeLimitDegrees{};
    float maxMoveSpeed{};
};

struct CharacterMoveIntent {
    Vec3 desiredDirection{};
    float dt{};
    bool jumpRequested{};
};

struct CharacterMoveResult {
    Vec3 position{};
    Vec3 velocity{};
    bool grounded{};
    bool hitCeiling{};
};

CharacterMoveResult moveCharacter(const PhysicsScene& scene,
                                  PhysicsWorldId world,
                                  BodyId body,
                                  const CharacterMoveIntent& intent);

} // namespace threadmaxx::physics
```

### 5.5 Contact events

```cpp id="m0q4zc"
namespace threadmaxx::physics {

struct ContactEvent {
    BodyId bodyA{};
    BodyId bodyB{};
    Vec3 point{};
    Vec3 normal{};
    float impulse{};
    bool began{};
    bool ended{};
};

using ContactCallback = std::function<void(const ContactEvent&)>;

} // namespace threadmaxx::physics
```

## 6. Integration with `threadmaxx`

The engine-side contract is intentionally small.

The roadmap says the engine should provide the `PhysicsBodyRef` slot and a read-only world snapshot pattern; this library turns that into a usable physics pipeline.  

### 6.1 Component-driven sync

A game system can mirror chunked ECS state into physics bodies like this:

```cpp id="x4n1sd"
class PhysicsSyncSystem final : public threadmaxx::ISystem {
public:
    void update(threadmaxx::SystemContext& ctx) override {
        ctx.forEachChunk<
            threadmaxx::PhysicsBodyRef,
            threadmaxx::Transform,
            threadmaxx::Velocity
        >([&](auto& chunk) {
            auto bodies = chunk.span<threadmaxx::PhysicsBodyRef>();
            auto transforms = chunk.span<threadmaxx::Transform>();
            auto velocities = chunk.span<threadmaxx::Velocity>();

            // push game state into physics, or read back from physics
        });
    }
};
```

### 6.2 Read-only world snapshot

Physics should consume a snapshot of the game world for collision filtering, triggers, and kinematic queries. The engine roadmap already treats read-only snapshots as the right boundary for physics integration. 

### 6.3 Step order

A stable step order is:

1. collect game intents,
2. sync game state into physics,
3. step the physics world,
4. pull back body transforms,
5. emit contacts / triggers,
6. write results to the game’s command buffer.

That keeps the authority boundary obvious and avoids hidden cross-thread coupling.

## 7. What the library should not do

* no solver hardcoding in the engine,
* no physics-owned ECS,
* no renderer integration,
* no navmesh generation,
* no networking/rollback,
* no animation math,
* no editor UI,
* no implicit scene graph,
* no hidden ownership of game objects.

The roadmap is very explicit that Bullet / Jolt / PhysX each impose different world-ownership models, so the engine should not hardcode one. 

## 8. Implementation order

1. backend interface,
2. shape registry,
3. rigid body create/destroy,
4. world stepping,
5. body-state sync,
6. raycast and sweep queries,
7. contact events,
8. constraints,
9. character controller,
10. serialization and diagnostics.

## 9. Tests to add

* backend conformance tests against a stub solver,
* raycast/sweep/overlap correctness,
* stepping determinism on fixed input,
* body sync round-trip tests,
* constraint stability tests,
* character controller slope/step tests,
* contact begin/end event tests,
* large-world and many-body stress tests,
* save/load round-trip tests.
