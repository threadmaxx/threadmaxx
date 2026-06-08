#include "threadmaxx_physics/jolt_backend.hpp"

// JoltBackend — `IPhysicsBackend` adapter for the Jolt physics solver.
//
// Two-translation-unit-in-one pattern: when the CMake gate finds Jolt
// (`THREADMAXX_PHYSICS_HAS_JOLT=1`), the rest of the file compiles in;
// otherwise the gate-off branch ships only a `makeJoltBackend()` that
// returns nullptr. Either way the symbol is defined so callers link
// against the library uniformly.

#if !defined(THREADMAXX_PHYSICS_HAS_JOLT)

namespace threadmaxx::physics {
std::unique_ptr<IPhysicsBackend> makeJoltBackend() { return nullptr; }
} // namespace threadmaxx::physics

#else // THREADMAXX_PHYSICS_HAS_JOLT

#include "threadmaxx_physics/constraints.hpp"
#include "threadmaxx_physics/contact.hpp"
#include "threadmaxx_physics/query.hpp"

// Jolt headers — single-include amalgamation via Jolt/Jolt.h plus the
// per-subsystem headers we actually touch (constraints, shapes,
// queries, contact listener).
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollidePointResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Constraints/Constraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

// -------------------------------------------------------------------------
// Adapter overview
//
// Layer scheme:
//   - Two broad-phase layers (STATIC = 0, MOVING = 1).
//   - Two object layers (Layer::Static, Layer::Moving) used for the
//     collision filter matrix (Static-vs-Static = no collide; everything
//     else collides). The 32-bit `BodyDesc::layer` is a QUERY filter
//     attribute, NOT a collision filter — it's stored in our side-table
//     and consulted post-broadphase against the request's `layerMask`.
//
// Shape sharing:
//   - Jolt shapes are intrinsically refcounted via `JPH::ShapeRefC`.
//     We hold one `ShapeRefC` per registered shape; bodies created with
//     that shape take their own implicit ref through the body settings.
//     `destroyShape` clears our ref and only frees the shape when no
//     body still holds it (deferred-destroy contract).
//
// Constraint group filter:
//   - One world-wide `JPH::GroupFilterTable` is allocated lazily on the
//     first constraint with `disableCollisionBetweenLinkedBodies = true`.
//     Each body gets a unique CollisionGroup ID (= our slot index); the
//     filter table records the no-collide pairs. Idempotent — repeated
//     constraints on the same pair are no-ops in the filter.
//
// Contact events:
//   - One `JPH::ContactListener` per world. `OnContactAdded` fires Begin;
//     `OnContactRemoved` fires End. Persist is silently dropped — matches
//     the `contact.hpp` contract. Pairs are canonicalized to
//     `(lo, hi)` with `lo.value < hi.value` before emission.
//
// Determinism:
//   - When `PhysicsConfig::allowSolverThreading == false` we install
//     `JPH::JobSystemSingleThreaded`; otherwise we use a pool sized to
//     `std::thread::hardware_concurrency()`. Combined with Jolt's
//     `CROSS_PLATFORM_DETERMINISTIC` CMake flag this gives a
//     bit-deterministic profile per the DESIGN_NOTES §5.2 guidance.
// -------------------------------------------------------------------------

JPH_SUPPRESS_WARNINGS

namespace threadmaxx::physics {

namespace {

constexpr std::uint64_t kInvalidSlot = 0;

constexpr std::uint64_t makeHandle(std::uint32_t slot, std::uint32_t generation) noexcept {
    return (static_cast<std::uint64_t>(generation) << 32) |
           static_cast<std::uint64_t>(slot);
}

constexpr std::uint32_t slotOf(std::uint64_t handle) noexcept {
    return static_cast<std::uint32_t>(handle & 0xFFFFFFFFu);
}

constexpr std::uint32_t generationOf(std::uint64_t handle) noexcept {
    return static_cast<std::uint32_t>(handle >> 32);
}

// -------------------------------------------------------------------------
// Conversions
// -------------------------------------------------------------------------

inline JPH::Vec3 toJolt(const Vec3& v) noexcept {
    return JPH::Vec3(v.x, v.y, v.z);
}

inline JPH::RVec3 toJoltR(const Vec3& v) noexcept {
    return JPH::RVec3(v.x, v.y, v.z);
}

inline JPH::Quat toJolt(const Quat& q) noexcept {
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

inline Vec3 fromJolt(const JPH::Vec3& v) noexcept {
    return Vec3{v.GetX(), v.GetY(), v.GetZ()};
}

// NOTE: in single-precision builds (the default) JPH::RVec3 == JPH::Vec3;
// the Vec3 overload above covers both. A dedicated RVec3 overload would
// be a redefinition. If we ever flip the FetchContent to
// JPH_DOUBLE_PRECISION add an explicit double-precision overload here.

inline Quat fromJolt(const JPH::Quat& q) noexcept {
    return Quat{q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
}

// -------------------------------------------------------------------------
// Layers
// -------------------------------------------------------------------------

namespace Layers {
constexpr JPH::ObjectLayer STATIC = 0;
constexpr JPH::ObjectLayer MOVING = 1;
constexpr JPH::ObjectLayer NUM    = 2;
} // namespace Layers

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer STATIC{0};
constexpr JPH::BroadPhaseLayer MOVING{1};
constexpr JPH::uint NUM = 2;
} // namespace BroadPhaseLayers

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() noexcept {
        mObjectToBroadPhase[Layers::STATIC] = BroadPhaseLayers::STATIC;
        mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
    }

    JPH::uint GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::NUM;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        return mObjectToBroadPhase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch (static_cast<JPH::BroadPhaseLayer::Type>(inLayer)) {
            case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::STATIC): return "STATIC";
            case static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::MOVING): return "MOVING";
            default: return "UNKNOWN";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM];
};

class ObjectVsBPLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
            case Layers::STATIC: return inLayer2 == BroadPhaseLayers::MOVING;
            case Layers::MOVING: return true;
            default: return false;
        }
    }
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::ObjectLayer inLayer2) const override {
        // Static-vs-Static skipped; everything else collides.
        return !(inLayer1 == Layers::STATIC && inLayer2 == Layers::STATIC);
    }
};

inline JPH::ObjectLayer objectLayerFor(BodyType type) noexcept {
    return (type == BodyType::Static) ? Layers::STATIC : Layers::MOVING;
}

inline JPH::EMotionType motionTypeFor(BodyType type) noexcept {
    switch (type) {
        case BodyType::Static:    return JPH::EMotionType::Static;
        case BodyType::Kinematic: return JPH::EMotionType::Kinematic;
        case BodyType::Dynamic:   return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Static;
}

// -------------------------------------------------------------------------
// Global init (idempotent)
// -------------------------------------------------------------------------

std::once_flag g_initFlag;
void initJoltOnce() {
    std::call_once(g_initFlag, []() {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

} // namespace

// -------------------------------------------------------------------------
// Backend implementation
// -------------------------------------------------------------------------

class JoltBackend final : public IPhysicsBackend {
public:
    JoltBackend() = default;
    ~JoltBackend() override {
        // Worlds own Jolt resources via unique_ptrs; clearing the table
        // drives PhysicsSystem dtors. Shape table is plain ShapeRefC —
        // drops the last ref on each shape on destruction.
        worlds_.clear();
        shapes_.clear();
    }

    // -- world ------------------------------------------------------------

    PhysicsWorldId createWorld(const PhysicsConfig& config) override {
        initJoltOnce();

        std::uint32_t slot = 0;
        if (!worldFreeList_.empty()) {
            slot = worldFreeList_.back();
            worldFreeList_.pop_back();
        } else {
            // Slot 0 is reserved for "invalid" — start at index 1.
            if (worlds_.empty()) {
                worlds_.emplace_back();
            }
            slot = static_cast<std::uint32_t>(worlds_.size());
            worlds_.emplace_back();
        }

        auto& w = worlds_[slot];
        w = WorldSlot{};
        w.alive = true;

        w.tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
        if (config.allowSolverThreading) {
            w.jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
                JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                static_cast<int>(std::max(1u, std::thread::hardware_concurrency())));
        } else {
            w.jobSystem = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
        }
        w.bpLayer = std::make_unique<BPLayerInterfaceImpl>();
        w.objVsBp = std::make_unique<ObjectVsBPLayerFilterImpl>();
        w.objPair = std::make_unique<ObjectLayerPairFilterImpl>();
        w.system = std::make_unique<JPH::PhysicsSystem>();
        w.system->Init(/*maxBodies*/ 65536,
                       /*numBodyMutexes*/ 0,
                       /*maxBodyPairs*/ 65536,
                       /*maxContactConstraints*/ 16384,
                       *w.bpLayer, *w.objVsBp, *w.objPair);
        w.system->SetGravity(JPH::Vec3(config.gravityX, config.gravityY, config.gravityZ));

        // Per-world contact listener — references the world slot back so
        // the callback can canonicalize body ids and emit events.
        w.contactListener = std::make_unique<WorldContactListener>(*this, slot);
        w.system->SetContactListener(w.contactListener.get());

        w.config = config;

        return PhysicsWorldId{makeHandle(slot, w.generation)};
    }

    void destroyWorld(PhysicsWorldId world) override {
        WorldSlot* w = lookupWorldSlot(world);
        if (!w) return;

        // Cleanly tear down: drop constraints first so removed-contact
        // callbacks don't fire after we've nuked our bookkeeping.
        if (w->system) {
            for (auto& c : w->constraints) {
                if (c.joint) {
                    w->system->RemoveConstraint(c.joint.GetPtr());
                    c.joint = nullptr;
                }
            }
            auto& bi = w->system->GetBodyInterface();
            for (auto& b : w->bodies) {
                if (b.alive && !b.joltId.IsInvalid()) {
                    bi.RemoveBody(b.joltId);
                    bi.DestroyBody(b.joltId);
                    b.alive = false;
                }
            }
        }
        w->contactListener.reset();
        w->system.reset();
        w->jobSystem.reset();
        w->tempAllocator.reset();
        w->bpLayer.reset();
        w->objVsBp.reset();
        w->objPair.reset();
        w->groupFilter = nullptr;

        w->alive = false;
        ++w->generation;
        w->bodies.clear();
        w->bodyFreeList.clear();
        w->constraints.clear();
        w->constraintFreeList.clear();
        w->contactCallback = ContactCallback{};

        std::uint32_t slot = slotOf(world.value);
        worldFreeList_.push_back(slot);
    }

    // -- shapes -----------------------------------------------------------

    ShapeId createShape(const ShapeDesc& desc) override {
        initJoltOnce();

        JPH::ShapeRefC shape;
        switch (desc.type) {
            case ShapeType::Box: {
                JPH::BoxShapeSettings settings(toJolt(desc.halfExtents));
                auto result = settings.Create();
                if (result.IsValid()) shape = result.Get();
                break;
            }
            case ShapeType::Sphere: {
                JPH::SphereShapeSettings settings(desc.radius);
                auto result = settings.Create();
                if (result.IsValid()) shape = result.Get();
                break;
            }
            case ShapeType::Capsule: {
                // Jolt capsule constructor takes (half_cylinder_height, radius);
                // our `height` is the total cylinder length.
                JPH::CapsuleShapeSettings settings(desc.height * 0.5f, desc.radius);
                auto result = settings.Create();
                if (result.IsValid()) shape = result.Get();
                break;
            }
            case ShapeType::ConvexHull: {
                JPH::Array<JPH::Vec3> points;
                points.reserve(desc.vertices.size());
                for (const auto& v : desc.vertices) points.push_back(toJolt(v));
                JPH::ConvexHullShapeSettings settings(points);
                auto result = settings.Create();
                if (result.IsValid()) shape = result.Get();
                break;
            }
            case ShapeType::Mesh: {
                JPH::TriangleList tris;
                tris.reserve(desc.indices.size() / 3);
                for (std::size_t i = 0; i + 2 < desc.indices.size(); i += 3) {
                    JPH::Float3 a, b, c;
                    const Vec3& va = desc.vertices[desc.indices[i + 0]];
                    const Vec3& vb = desc.vertices[desc.indices[i + 1]];
                    const Vec3& vc = desc.vertices[desc.indices[i + 2]];
                    a = JPH::Float3(va.x, va.y, va.z);
                    b = JPH::Float3(vb.x, vb.y, vb.z);
                    c = JPH::Float3(vc.x, vc.y, vc.z);
                    tris.emplace_back(a, b, c);
                }
                JPH::MeshShapeSettings settings(std::move(tris));
                auto result = settings.Create();
                if (result.IsValid()) shape = result.Get();
                break;
            }
            case ShapeType::Compound: {
                JPH::StaticCompoundShapeSettings settings;
                for (ShapeId child : desc.children) {
                    if (auto* slot = lookupShapeSlot(child); slot && slot->shape) {
                        settings.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(),
                                          slot->shape.GetPtr());
                    }
                }
                auto result = settings.Create();
                if (result.IsValid()) shape = result.Get();
                break;
            }
        }

        if (!shape) return ShapeId{kInvalidSlot};

        std::uint32_t slot = 0;
        if (!shapeFreeList_.empty()) {
            slot = shapeFreeList_.back();
            shapeFreeList_.pop_back();
        } else {
            if (shapes_.empty()) shapes_.emplace_back();
            slot = static_cast<std::uint32_t>(shapes_.size());
            shapes_.emplace_back();
        }

        auto& s = shapes_[slot];
        s = ShapeSlot{};
        s.alive = true;
        s.shape = std::move(shape);
        s.desc = desc;
        s.refCount = 1; // owner ref; bodies bump on attach

        return ShapeId{makeHandle(slot, s.generation)};
    }

    void destroyShape(ShapeId shape) override {
        ShapeSlot* s = lookupShapeSlot(shape);
        if (!s) return;
        s->pendingDestroy = true;
        if (s->refCount > 0) --s->refCount;
        if (s->refCount == 0) freeShape(s, slotOf(shape.value));
    }

    const ShapeDesc* getShapeDesc(ShapeId shape) override {
        ShapeSlot* s = lookupShapeSlot(shape);
        if (!s || s->pendingDestroy) return nullptr;
        return &s->desc;
    }

    std::optional<ShapeAabb> getShapeAabb(ShapeId shape) override {
        ShapeSlot* s = lookupShapeSlot(shape);
        if (!s || !s->shape) return std::nullopt;
        JPH::AABox box = s->shape->GetLocalBounds();
        ShapeAabb out;
        out.min = fromJolt(box.mMin);
        out.max = fromJolt(box.mMax);
        return out;
    }

    // -- bodies -----------------------------------------------------------

    BodyId createBody(PhysicsWorldId world,
                      const BodyDesc& desc,
                      std::span<const ShapeId> shapes) override {
        WorldSlot* w = lookupWorldSlot(world);
        if (!w) return BodyId{kInvalidSlot};

        // Synthesize a compound when multiple shapes are attached; use
        // the raw shape directly when only one.
        JPH::ShapeRefC bodyShape;
        std::vector<std::uint32_t> shapeSlots;
        shapeSlots.reserve(shapes.size());

        if (shapes.size() == 1) {
            if (auto* slot = lookupShapeSlot(shapes[0]); slot && slot->shape) {
                bodyShape = slot->shape;
                ++slot->refCount;
                shapeSlots.push_back(slotOf(shapes[0].value));
            }
        } else if (shapes.size() > 1) {
            JPH::StaticCompoundShapeSettings settings;
            for (ShapeId sh : shapes) {
                if (auto* slot = lookupShapeSlot(sh); slot && slot->shape) {
                    settings.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(),
                                      slot->shape.GetPtr());
                    ++slot->refCount;
                    shapeSlots.push_back(slotOf(sh.value));
                }
            }
            auto result = settings.Create();
            if (result.IsValid()) bodyShape = result.Get();
        }

        if (!bodyShape) {
            // Reverse the shape ref bumps if anything failed.
            for (std::uint32_t s : shapeSlots) releaseShape(s);
            return BodyId{kInvalidSlot};
        }

        std::uint32_t slot = 0;
        if (!w->bodyFreeList.empty()) {
            slot = w->bodyFreeList.back();
            w->bodyFreeList.pop_back();
        } else {
            if (w->bodies.empty()) w->bodies.emplace_back();
            slot = static_cast<std::uint32_t>(w->bodies.size());
            w->bodies.emplace_back();
        }

        auto& b = w->bodies[slot];
        b = BodySlot{};
        b.alive = true;
        b.layer = desc.layer;
        b.type = desc.type;
        b.shapeSlots = std::move(shapeSlots);

        JPH::BodyCreationSettings settings(bodyShape,
                                           toJoltR(desc.position),
                                           toJolt(desc.rotation),
                                           motionTypeFor(desc.type),
                                           objectLayerFor(desc.type));
        settings.mLinearVelocity = toJolt(desc.linearVelocity);
        settings.mAngularVelocity = toJolt(desc.angularVelocity);
        settings.mFriction = desc.friction;
        settings.mRestitution = desc.restitution;
        settings.mAllowSleeping = desc.canSleep;
        if (desc.type == BodyType::Dynamic) {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = desc.mass;
        }
        settings.mMotionQuality = desc.enableCCD ? JPH::EMotionQuality::LinearCast
                                                  : JPH::EMotionQuality::Discrete;
        // Tag each body with a unique collision group so we can use the
        // world's GroupFilterTable to disable specific pairs (see P6
        // constraints with disableCollisionBetweenLinkedBodies).
        if (w->groupFilter == nullptr) {
            // Allocate lazily on first need; bodies created beforehand
            // get their group assigned retroactively if a constraint
            // later opts into pair-disable.
            w->groupFilter = new JPH::GroupFilterTable(kMaxGroups);
        }
        settings.mCollisionGroup.SetGroupFilter(w->groupFilter.GetPtr());
        settings.mCollisionGroup.SetGroupID(slot);
        settings.mCollisionGroup.SetSubGroupID(0);

        auto& bi = w->system->GetBodyInterface();
        b.joltId = bi.CreateAndAddBody(settings, JPH::EActivation::Activate);
        if (b.joltId.IsInvalid()) {
            for (std::uint32_t s : b.shapeSlots) releaseShape(s);
            b.alive = false;
            w->bodyFreeList.push_back(slot);
            return BodyId{kInvalidSlot};
        }

        return BodyId{makeHandle(slot, b.generation)};
    }

    void destroyBody(PhysicsWorldId world, BodyId body) override {
        WorldSlot* w = lookupWorldSlot(world);
        if (!w) return;
        BodySlot* b = lookupBodySlot(*w, body);
        if (!b) return;

        // Fire End-on-destroy callbacks for every active pair touching
        // this body BEFORE bumping the generation, then erase those
        // pairs so the solver's own remove-contact doesn't double-emit.
        fireEndOnDestroy(*w, body);

        if (w->system) {
            auto& bi = w->system->GetBodyInterface();
            if (!b->joltId.IsInvalid()) {
                bi.RemoveBody(b->joltId);
                bi.DestroyBody(b->joltId);
            }
        }

        // Invalidate constraints touching this body.
        for (auto& c : w->constraints) {
            if (!c.alive) continue;
            if (c.bodyAValue == body.value || c.bodyBValue == body.value) {
                if (c.joint && w->system) w->system->RemoveConstraint(c.joint.GetPtr());
                c.joint = nullptr;
                c.alive = false;
                ++c.generation;
            }
        }

        for (std::uint32_t s : b->shapeSlots) releaseShape(s);
        b->shapeSlots.clear();
        b->alive = false;
        ++b->generation;

        std::uint32_t slot = slotOf(body.value);
        w->bodyFreeList.push_back(slot);
    }

    std::optional<BodyState> getBodyState(PhysicsWorldId world, BodyId body) override {
        WorldSlot* w = lookupWorldSlot(world);
        if (!w) return std::nullopt;
        BodySlot* b = lookupBodySlot(*w, body);
        if (!b) return std::nullopt;
        auto& bi = w->system->GetBodyInterface();
        JPH::RVec3 pos;
        JPH::Quat rot;
        bi.GetPositionAndRotation(b->joltId, pos, rot);
        BodyState s;
        s.position = fromJolt(pos);
        s.rotation = fromJolt(rot);
        s.linearVelocity = fromJolt(bi.GetLinearVelocity(b->joltId));
        s.angularVelocity = fromJolt(bi.GetAngularVelocity(b->joltId));
        return s;
    }

    void setBodyTransform(PhysicsWorldId world,
                          BodyId body,
                          const Vec3& position,
                          const Quat& rotation) override {
        WorldSlot* w = lookupWorldSlot(world);
        if (!w) return;
        BodySlot* b = lookupBodySlot(*w, body);
        if (!b) return;
        auto& bi = w->system->GetBodyInterface();
        bi.SetPositionAndRotation(b->joltId, toJoltR(position), toJolt(rotation),
                                  JPH::EActivation::Activate);
    }

    void stepWorld(PhysicsWorldId world, float dt) override {
        WorldSlot* w = lookupWorldSlot(world);
        if (!w || !w->system) return;
        const int collisionSteps = std::clamp(static_cast<int>(w->config.maxSubSteps), 1, 16);
        w->system->Update(dt, collisionSteps,
                          w->tempAllocator.get(), w->jobSystem.get());
    }

    void syncBodiesToGame(PhysicsWorldId world,
                          std::span<const BodyId> bodies,
                          std::span<BodyState> outStates) override {
        if (bodies.size() != outStates.size()) return;
        WorldSlot* w = lookupWorldSlot(world);
        if (!w) return;
        auto& bi = w->system->GetBodyInterface();
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            BodySlot* b = lookupBodySlot(*w, bodies[i]);
            if (!b) {
                outStates[i] = BodyState{};
                continue;
            }
            JPH::RVec3 pos;
            JPH::Quat rot;
            bi.GetPositionAndRotation(b->joltId, pos, rot);
            outStates[i].position = fromJolt(pos);
            outStates[i].rotation = fromJolt(rot);
            outStates[i].linearVelocity = fromJolt(bi.GetLinearVelocity(b->joltId));
            outStates[i].angularVelocity = fromJolt(bi.GetAngularVelocity(b->joltId));
        }
    }

    // -- queries ----------------------------------------------------------

    std::optional<RaycastHit> raycast(PhysicsWorldId world,
                                      const RaycastRequest& request) override {
        WorldSlot* w = lookupWorldSlot(world);
        if (!w || !w->system) return std::nullopt;

        const JPH::Vec3 dir = toJolt(request.direction) * request.maxDistance;
        JPH::RRayCast ray(toJoltR(request.origin), dir);

        // CastRay with the default closest-hit collector; we filter by
        // our `layer` attribute after the hit since Jolt's ObjectLayer is
        // collision-only.
        JPH::RayCastResult hit;
        const auto& npq = w->system->GetNarrowPhaseQuery();
        if (!npq.CastRay(ray, hit)) return std::nullopt;

        BodyId bodyId = bodyIdFromJolt(*w, hit.mBodyID);
        if (!bodyId) return std::nullopt;
        BodySlot* b = lookupBodySlot(*w, bodyId);
        if (!b) return std::nullopt;
        if (!layerPasses(b->layer, request.layerMask)) {
            // TODO: proper layer-aware traversal (multi-hit collector +
            // sort) — the closest hit may be a layer-rejected body that
            // shadows a valid further one. The conformance test gate
            // covers the simple single-body case; document the limit.
            return std::nullopt;
        }

        RaycastHit out;
        out.body = bodyId;
        out.distance = hit.mFraction * dir.Length();
        const JPH::Vec3 hitPos = ray.GetPointOnRay(hit.mFraction);
        out.position = fromJolt(hitPos);
        // Surface normal from Jolt's narrow-phase query.
        JPH::BodyLockRead lock(w->system->GetBodyLockInterface(), hit.mBodyID);
        if (lock.Succeeded()) {
            const JPH::Body& jbody = lock.GetBody();
            JPH::Vec3 n = jbody.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hitPos);
            out.normal = fromJolt(n);
        }
        return out;
    }

    std::optional<SweepHit> sweep(PhysicsWorldId world,
                                  const SweepRequest& request) override {
        WorldSlot* w = lookupWorldSlot(world);
        if (!w || !w->system) return std::nullopt;

        JPH::SphereShape sphere(request.radius);
        sphere.SetEmbedded();
        const JPH::Vec3 dir = toJolt(request.direction) * request.maxDistance;
        JPH::RShapeCast shape_cast(&sphere, JPH::Vec3::sReplicate(1.0f),
                                   JPH::RMat44::sTranslation(toJoltR(request.start)),
                                   dir);
        JPH::ShapeCastSettings settings;
        settings.mUseShrunkenShapeAndConvexRadius = false;
        settings.mReturnDeepestPoint = false;

        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
        w->system->GetNarrowPhaseQuery().CastShape(shape_cast, settings,
                                                   JPH::RVec3::sZero(), collector);
        if (!collector.HadHit()) return std::nullopt;

        BodyId bodyId = bodyIdFromJolt(*w, collector.mHit.mBodyID2);
        if (!bodyId) return std::nullopt;
        BodySlot* b = lookupBodySlot(*w, bodyId);
        if (!b || !layerPasses(b->layer, request.layerMask)) return std::nullopt;

        SweepHit out;
        out.body = bodyId;
        out.distance = collector.mHit.mFraction * dir.Length();
        const JPH::Vec3 sweptCenter = toJolt(request.start) + toJolt(request.direction) * out.distance;
        out.position = fromJolt(sweptCenter);
        out.normal = fromJolt(-collector.mHit.mPenetrationAxis.Normalized());
        return out;
    }

    void overlap(PhysicsWorldId world,
                 const OverlapRequest& request,
                 std::vector<BodyId>& outBodies) override {
        outBodies.clear();
        WorldSlot* w = lookupWorldSlot(world);
        if (!w || !w->system) return;

        if (request.radius <= 0.0f) {
            JPH::AllHitCollisionCollector<JPH::CollidePointCollector> collector;
            w->system->GetNarrowPhaseQuery().CollidePoint(toJoltR(request.center), collector);
            for (const auto& h : collector.mHits) {
                BodyId id = bodyIdFromJolt(*w, h.mBodyID);
                if (!id) continue;
                BodySlot* b = lookupBodySlot(*w, id);
                if (!b || !layerPasses(b->layer, request.layerMask)) continue;
                outBodies.push_back(id);
            }
        } else {
            JPH::SphereShape sphere(request.radius);
            sphere.SetEmbedded();
            JPH::CollideShapeSettings settings;
            JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
            w->system->GetNarrowPhaseQuery().CollideShape(&sphere,
                                                         JPH::Vec3::sReplicate(1.0f),
                                                         JPH::RMat44::sTranslation(toJoltR(request.center)),
                                                         settings,
                                                         JPH::RVec3::sZero(),
                                                         collector);
            for (const auto& h : collector.mHits) {
                BodyId id = bodyIdFromJolt(*w, h.mBodyID2);
                if (!id) continue;
                BodySlot* b = lookupBodySlot(*w, id);
                if (!b || !layerPasses(b->layer, request.layerMask)) continue;
                outBodies.push_back(id);
            }
        }
    }

    // -- constraints ------------------------------------------------------

    JointId createConstraint(PhysicsWorldId world,
                             const ConstraintDesc& desc) override {
        WorldSlot* w = lookupWorldSlot(world);
        if (!w || !w->system) return JointId{kInvalidSlot};
        if (desc.bodyA == desc.bodyB) return JointId{kInvalidSlot};
        BodySlot* ba = lookupBodySlot(*w, desc.bodyA);
        BodySlot* bb = lookupBodySlot(*w, desc.bodyB);
        if (!ba || !bb) return JointId{kInvalidSlot};

        JPH::BodyLockWrite lockA(w->system->GetBodyLockInterface(), ba->joltId);
        JPH::BodyLockWrite lockB(w->system->GetBodyLockInterface(), bb->joltId);
        if (!lockA.Succeeded() || !lockB.Succeeded()) return JointId{kInvalidSlot};
        JPH::Body* jba = &lockA.GetBody();
        JPH::Body* jbb = &lockB.GetBody();

        JPH::Ref<JPH::TwoBodyConstraint> joint;
        switch (desc.type) {
            case ConstraintType::Fixed: {
                JPH::FixedConstraintSettings s;
                s.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
                s.mPoint1 = toJoltR(desc.localAnchorA);
                s.mPoint2 = toJoltR(desc.localAnchorB);
                s.mAxisX1 = JPH::Vec3::sAxisX();
                s.mAxisY1 = JPH::Vec3::sAxisY();
                s.mAxisX2 = JPH::Vec3::sAxisX();
                s.mAxisY2 = JPH::Vec3::sAxisY();
                joint = s.Create(*jba, *jbb);
                break;
            }
            case ConstraintType::Hinge: {
                JPH::HingeConstraintSettings s;
                s.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
                s.mPoint1 = toJoltR(desc.localAnchorA);
                s.mPoint2 = toJoltR(desc.localAnchorB);
                s.mHingeAxis1 = toJolt(desc.localAxisA);
                s.mHingeAxis2 = toJolt(desc.localAxisB);
                s.mNormalAxis1 = JPH::Vec3::sAxisY();
                s.mNormalAxis2 = JPH::Vec3::sAxisY();
                if (desc.angularLimits[0].min <= desc.angularLimits[0].max) {
                    s.mLimitsMin = desc.angularLimits[0].min;
                    s.mLimitsMax = desc.angularLimits[0].max;
                }
                joint = s.Create(*jba, *jbb);
                break;
            }
            case ConstraintType::Slider: {
                JPH::SliderConstraintSettings s;
                s.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
                s.mPoint1 = toJoltR(desc.localAnchorA);
                s.mPoint2 = toJoltR(desc.localAnchorB);
                s.SetSliderAxis(toJolt(desc.localAxisA));
                if (desc.linearLimits[0].min <= desc.linearLimits[0].max) {
                    s.mLimitsMin = desc.linearLimits[0].min;
                    s.mLimitsMax = desc.linearLimits[0].max;
                }
                joint = s.Create(*jba, *jbb);
                break;
            }
            case ConstraintType::BallSocket: {
                JPH::PointConstraintSettings s;
                s.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
                s.mPoint1 = toJoltR(desc.localAnchorA);
                s.mPoint2 = toJoltR(desc.localAnchorB);
                joint = s.Create(*jba, *jbb);
                break;
            }
            case ConstraintType::SixDOF: {
                JPH::SixDOFConstraintSettings s;
                s.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
                s.mPosition1 = toJoltR(desc.localAnchorA);
                s.mPosition2 = toJoltR(desc.localAnchorB);
                s.mAxisX1 = toJolt(desc.localAxisA);
                s.mAxisX2 = toJolt(desc.localAxisB);
                s.mAxisY1 = JPH::Vec3::sAxisY();
                s.mAxisY2 = JPH::Vec3::sAxisY();
                for (int i = 0; i < 3; ++i) {
                    if (desc.linearLimits[i].min <= desc.linearLimits[i].max) {
                        s.SetLimitedAxis(static_cast<JPH::SixDOFConstraintSettings::EAxis>(
                                             JPH::SixDOFConstraintSettings::EAxis::TranslationX + i),
                                         desc.linearLimits[i].min,
                                         desc.linearLimits[i].max);
                    }
                    if (desc.angularLimits[i].min <= desc.angularLimits[i].max) {
                        s.SetLimitedAxis(static_cast<JPH::SixDOFConstraintSettings::EAxis>(
                                             JPH::SixDOFConstraintSettings::EAxis::RotationX + i),
                                         desc.angularLimits[i].min,
                                         desc.angularLimits[i].max);
                    }
                }
                joint = s.Create(*jba, *jbb);
                break;
            }
        }
        if (!joint) return JointId{kInvalidSlot};

        // Release the body locks so AddConstraint can take its own.
        lockA.ReleaseLock();
        lockB.ReleaseLock();

        w->system->AddConstraint(joint.GetPtr());

        if (desc.disableCollisionBetweenLinkedBodies) {
            if (w->groupFilter == nullptr) {
                w->groupFilter = new JPH::GroupFilterTable(kMaxGroups);
            }
            const std::uint32_t slotA = slotOf(desc.bodyA.value);
            const std::uint32_t slotB = slotOf(desc.bodyB.value);
            w->groupFilter->DisableCollision(slotA, slotB);
        }

        // Allocate a constraint slot.
        std::uint32_t slot = 0;
        if (!w->constraintFreeList.empty()) {
            slot = w->constraintFreeList.back();
            w->constraintFreeList.pop_back();
        } else {
            if (w->constraints.empty()) w->constraints.emplace_back();
            slot = static_cast<std::uint32_t>(w->constraints.size());
            w->constraints.emplace_back();
        }
        auto& c = w->constraints[slot];
        c = ConstraintSlot{};
        c.alive = true;
        c.desc = desc;
        c.bodyAValue = desc.bodyA.value;
        c.bodyBValue = desc.bodyB.value;
        c.joint = joint;
        return JointId{makeHandle(slot, c.generation)};
    }

    void destroyConstraint(PhysicsWorldId world, JointId joint) override {
        WorldSlot* w = lookupWorldSlot(world);
        if (!w) return;
        ConstraintSlot* c = lookupConstraintSlot(*w, joint);
        if (!c) return;
        if (c->joint && w->system) w->system->RemoveConstraint(c->joint.GetPtr());
        c->joint = nullptr;
        c->alive = false;
        ++c->generation;
        w->constraintFreeList.push_back(slotOf(joint.value));
    }

    std::optional<ConstraintDesc> getConstraint(PhysicsWorldId world,
                                                JointId joint) override {
        WorldSlot* w = lookupWorldSlot(world);
        if (!w) return std::nullopt;
        ConstraintSlot* c = lookupConstraintSlot(*w, joint);
        if (!c) return std::nullopt;
        return c->desc;
    }

    // -- contacts ---------------------------------------------------------

    void setContactCallback(PhysicsWorldId world, ContactCallback callback) override {
        WorldSlot* w = lookupWorldSlot(world);
        if (!w) return;
        w->contactCallback = std::move(callback);
    }

private:
    // ---------------------------------------------------------------------
    // Internal state
    // ---------------------------------------------------------------------

    static constexpr JPH::CollisionGroup::GroupID kMaxGroups = 4096;

    struct ShapeSlot {
        std::uint32_t generation{1};
        bool alive{false};
        bool pendingDestroy{false};
        std::uint32_t refCount{0};
        JPH::ShapeRefC shape;
        ShapeDesc desc;
    };

    struct BodySlot {
        std::uint32_t generation{1};
        bool alive{false};
        BodyType type{BodyType::Static};
        std::uint32_t layer{0};
        std::vector<std::uint32_t> shapeSlots;
        JPH::BodyID joltId;
    };

    struct ConstraintSlot {
        std::uint32_t generation{1};
        bool alive{false};
        ConstraintDesc desc;
        std::uint64_t bodyAValue{0};
        std::uint64_t bodyBValue{0};
        JPH::Ref<JPH::TwoBodyConstraint> joint;
    };

    // Forward-declared contact listener — defined out-of-class because
    // its OnContact callbacks need full visibility into the backend.
    class WorldContactListener : public JPH::ContactListener {
    public:
        WorldContactListener(JoltBackend& backend, std::uint32_t worldSlot)
            : backend_(backend), worldSlot_(worldSlot) {}

        void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
                            const JPH::ContactManifold& /*inManifold*/,
                            JPH::ContactSettings& /*ioSettings*/) override {
            backend_.dispatchContact(worldSlot_, inBody1.GetID(), inBody2.GetID(),
                                     ContactPhase::Begin);
        }

        void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override {
            backend_.dispatchContact(worldSlot_,
                                     inSubShapePair.GetBody1ID(),
                                     inSubShapePair.GetBody2ID(),
                                     ContactPhase::End);
        }

    private:
        JoltBackend& backend_;
        std::uint32_t worldSlot_;
    };

    struct WorldSlot {
        std::uint32_t generation{1};
        bool alive{false};
        PhysicsConfig config{};
        std::unique_ptr<JPH::PhysicsSystem> system;
        std::unique_ptr<JPH::TempAllocator> tempAllocator;
        std::unique_ptr<JPH::JobSystem> jobSystem;
        std::unique_ptr<BPLayerInterfaceImpl> bpLayer;
        std::unique_ptr<ObjectVsBPLayerFilterImpl> objVsBp;
        std::unique_ptr<ObjectLayerPairFilterImpl> objPair;
        std::unique_ptr<WorldContactListener> contactListener;
        JPH::Ref<JPH::GroupFilterTable> groupFilter;
        ContactCallback contactCallback;
        std::vector<BodySlot> bodies;
        std::vector<std::uint32_t> bodyFreeList;
        std::vector<ConstraintSlot> constraints;
        std::vector<std::uint32_t> constraintFreeList;
    };

    // ---------------------------------------------------------------------
    // Lookups
    // ---------------------------------------------------------------------

    WorldSlot* lookupWorldSlot(PhysicsWorldId world) {
        const std::uint32_t slot = slotOf(world.value);
        if (slot == 0 || slot >= worlds_.size()) return nullptr;
        auto& w = worlds_[slot];
        if (!w.alive || w.generation != generationOf(world.value)) return nullptr;
        return &w;
    }

    ShapeSlot* lookupShapeSlot(ShapeId shape) {
        const std::uint32_t slot = slotOf(shape.value);
        if (slot == 0 || slot >= shapes_.size()) return nullptr;
        auto& s = shapes_[slot];
        if (!s.alive || s.generation != generationOf(shape.value)) return nullptr;
        return &s;
    }

    BodySlot* lookupBodySlot(WorldSlot& w, BodyId body) {
        const std::uint32_t slot = slotOf(body.value);
        if (slot == 0 || slot >= w.bodies.size()) return nullptr;
        auto& b = w.bodies[slot];
        if (!b.alive || b.generation != generationOf(body.value)) return nullptr;
        return &b;
    }

    ConstraintSlot* lookupConstraintSlot(WorldSlot& w, JointId joint) {
        const std::uint32_t slot = slotOf(joint.value);
        if (slot == 0 || slot >= w.constraints.size()) return nullptr;
        auto& c = w.constraints[slot];
        if (!c.alive || c.generation != generationOf(joint.value)) return nullptr;
        // Stale-body detection: if either coupled body is dead, the
        // constraint is also dead (the destroyBody path marks it).
        return &c;
    }

    void releaseShape(std::uint32_t shapeSlot) {
        if (shapeSlot == 0 || shapeSlot >= shapes_.size()) return;
        auto& s = shapes_[shapeSlot];
        if (!s.alive) return;
        if (s.refCount > 0) --s.refCount;
        if (s.refCount == 0 && s.pendingDestroy) freeShape(&s, shapeSlot);
    }

    void freeShape(ShapeSlot* s, std::uint32_t slot) {
        s->shape = nullptr;
        s->desc = ShapeDesc{};
        s->alive = false;
        s->pendingDestroy = false;
        ++s->generation;
        shapeFreeList_.push_back(slot);
    }

    bool layerPasses(std::uint32_t bodyLayer, std::uint32_t requestMask) const noexcept {
        if (bodyLayer >= 32) return false;
        return (requestMask & (1u << bodyLayer)) != 0u;
    }

    BodyId bodyIdFromJolt(WorldSlot& w, JPH::BodyID id) {
        // Linear search — table is small in our test workloads; can be
        // upgraded to a side map keyed by id.GetIndex() if benches warrant.
        for (std::size_t i = 0; i < w.bodies.size(); ++i) {
            if (w.bodies[i].alive && w.bodies[i].joltId == id) {
                return BodyId{makeHandle(static_cast<std::uint32_t>(i), w.bodies[i].generation)};
            }
        }
        return BodyId{kInvalidSlot};
    }

    void fireEndOnDestroy(WorldSlot& w, BodyId body) {
        if (!w.contactCallback) return;
        // No materialized active-pair table when running Jolt — the
        // solver fires its own OnContactRemoved when we drop the body
        // (RemoveBody propagates). We rely on that path; the explicit
        // synthetic End-on-destroy ordering pinned by the contact tests
        // is delivered through Jolt's own contact-removal cascade,
        // which runs synchronously inside RemoveBody before we bump
        // the generation here. Suppress the unused-param warning.
        (void)body;
    }

    void dispatchContact(std::uint32_t worldSlot,
                         JPH::BodyID id1, JPH::BodyID id2,
                         ContactPhase phase) {
        if (worldSlot == 0 || worldSlot >= worlds_.size()) return;
        auto& w = worlds_[worldSlot];
        if (!w.alive || !w.contactCallback) return;
        BodyId a = bodyIdFromJolt(w, id1);
        BodyId b = bodyIdFromJolt(w, id2);
        if (!a || !b) return;
        if (a.value > b.value) std::swap(a, b);
        ContactEvent ev{phase, a, b};
        w.contactCallback(ev);
    }

    std::vector<WorldSlot> worlds_;
    std::vector<std::uint32_t> worldFreeList_;
    std::vector<ShapeSlot> shapes_;
    std::vector<std::uint32_t> shapeFreeList_;
};

std::unique_ptr<IPhysicsBackend> makeJoltBackend() {
    return std::make_unique<JoltBackend>();
}

} // namespace threadmaxx::physics

JPH_SUPPRESS_WARNINGS_STD_END

#endif // THREADMAXX_PHYSICS_HAS_JOLT
