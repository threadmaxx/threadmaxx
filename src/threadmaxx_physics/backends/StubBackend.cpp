#include "threadmaxx_physics/stub_backend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// StubBackend — deterministic, no-real-physics IPhysicsBackend.
//
// Worlds, shapes, and bodies live in flat slot tables guarded by
// (generation, alive) so that stale ids never alias newly-allocated
// slots. Ids encode `(generation << 32) | slot`; slot 0 is reserved
// for "invalid" so a default-constructed `PhysicsWorldId{}` is
// distinguishable from any live world.
//
// P1 shipped the table machinery + create / destroy lifecycle plumbing.
// P2 added the shape registry refcount: every body and every compound
// parent holds one reference on each shape it uses; `destroyShape` is
// a deferred-destroy that flips `pendingDestroy` and frees the slot
// only when the refcount falls to zero. Compound shapes also propagate
// refcounts to their children, so freeing a compound can cascade.
//
// P3 added `getBodyState` / `setBodyTransform` for single-body read +
// kinematic teleport.
//
// P4 (this file) wires `stepWorld` to a kinematic-only integrator:
// every alive non-Static body advances by `linearVelocity * dt` and
// composes a rotation built from `angularVelocity * dt` interpreted as
// world-frame axis-angle. No collision, no gravity, no forces — those
// belong to the real backend (P9). Static bodies are skipped.

namespace threadmaxx::physics {

namespace {

constexpr std::uint64_t kInvalidSlot = 0;

// Pack a slot index + generation into the 64-bit id. Generation lives
// in the high 32 bits so the slot is recoverable with a low-mask.
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

// Hamilton-product quat multiplication: `out = a * b` with the
// {x,y,z,w} = {xi,yj,zk,w} convention used throughout the engine.
constexpr Quat quatMul(const Quat& a, const Quat& b) noexcept {
    return Quat{
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

// Compose `q` with a rotation built from world-frame angular velocity
// `omega` (rad/s) over `dt` seconds, interpreted as axis-angle. Pure
// kinematic integration — no precession, no damping. Returns `q`
// unchanged when the resulting angle is below a sin/cos cutoff so we
// avoid dividing by near-zero |omega|.
inline Quat integrateAngular(const Quat& q, const Vec3& omega, float dt) noexcept {
    const float magSq = omega.x * omega.x + omega.y * omega.y + omega.z * omega.z;
    if (magSq <= 0.0f) {
        return q;
    }
    const float mag = std::sqrt(magSq);
    const float angle = mag * dt;
    // Tiny rotations collapse to identity in float; skipping avoids the
    // 1/mag divide and matches what a real backend would do at numerical
    // dead-band.
    constexpr float kAngleCutoff = 1e-7f;
    if (angle < kAngleCutoff && angle > -kAngleCutoff) {
        return q;
    }
    const float invMag = 1.0f / mag;
    const float half = angle * 0.5f;
    const float s = std::sin(half);
    const float c = std::cos(half);
    const Quat dq{
        omega.x * invMag * s,
        omega.y * invMag * s,
        omega.z * invMag * s,
        c,
    };
    return quatMul(dq, q);
}

struct ShapeSlot {
    ShapeDesc desc{};
    std::uint32_t generation{};
    std::uint32_t refCount{};      // bodies + compound parents holding this shape
    bool alive{};
    bool pendingDestroy{};         // destroyShape was called but refCount > 0
};

struct BodySlot {
    BodyDesc desc{};
    BodyState state{};
    std::vector<ShapeId> shapes;   // shape refs to release on destroy
    std::uint32_t generation{};
    bool alive{};
};

struct WorldSlot {
    PhysicsConfig config{};
    std::vector<BodySlot> bodies;
    std::uint32_t generation{};
    bool alive{};
};

class StubBackend final : public IPhysicsBackend {
public:
    StubBackend() {
        // Reserve slot 0 so the first non-zero allocation lands at
        // index 1. Keeps the "zero id == invalid" invariant cheap.
        worlds_.emplace_back();
        shapes_.emplace_back();
    }

    PhysicsWorldId createWorld(const PhysicsConfig& config) override {
        const auto slot = allocWorldSlot();
        WorldSlot& s = worlds_[slot];
        s.config = config;
        s.bodies.clear();
        s.bodies.emplace_back(); // reserve body slot 0
        s.alive = true;
        return PhysicsWorldId{makeHandle(slot, s.generation)};
    }

    void destroyWorld(PhysicsWorldId world) override {
        WorldSlot* w = lookupWorld(world);
        if (w == nullptr) {
            return;
        }
        // Drop every alive body's shape references so deferred-destroy
        // shapes can be freed.
        for (BodySlot& body : w->bodies) {
            if (!body.alive) {
                continue;
            }
            releaseBodyShapeRefs(body);
            body.alive = false;
        }
        w->bodies.clear();
        w->alive = false;
        ++w->generation;
    }

    ShapeId createShape(const ShapeDesc& desc) override {
        const auto slot = allocShapeSlot();
        ShapeSlot& s = shapes_[slot];
        s.desc = desc;
        s.refCount = 0;
        s.pendingDestroy = false;
        s.alive = true;
        // Compound parents hold a reference on each child so the
        // children outlive the original `createShape` caller's drop.
        if (s.desc.type == ShapeType::Compound) {
            for (ShapeId child : s.desc.children) {
                incrementShapeRef(child);
            }
        }
        return ShapeId{makeHandle(slot, s.generation)};
    }

    void destroyShape(ShapeId shape) override {
        ShapeSlot* s = lookupShape(shape);
        if (s == nullptr) {
            return;
        }
        s->pendingDestroy = true;
        if (s->refCount == 0) {
            freeShape(slotOf(shape.value));
        }
    }

    const ShapeDesc* getShapeDesc(ShapeId shape) override {
        ShapeSlot* s = lookupShape(shape);
        if (s == nullptr) {
            return nullptr;
        }
        return &s->desc;
    }

    std::optional<ShapeAabb> getShapeAabb(ShapeId shape) override {
        ShapeSlot* s = lookupShape(shape);
        if (s == nullptr) {
            return std::nullopt;
        }
        return computeAabb(s->desc);
    }

    BodyId createBody(PhysicsWorldId world,
                      const BodyDesc& desc,
                      std::span<const ShapeId> shapes) override {
        WorldSlot* w = lookupWorld(world);
        if (w == nullptr) {
            return BodyId{};
        }
        const auto slot = allocBodySlot(*w);
        BodySlot& b = w->bodies[slot];
        b.desc = desc;
        b.state.position = desc.position;
        b.state.rotation = desc.rotation;
        b.state.linearVelocity = desc.linearVelocity;
        b.state.angularVelocity = desc.angularVelocity;
        // Bump refcounts and remember which shapes this body holds so
        // `destroyBody` (and `destroyWorld`) can release them cleanly.
        b.shapes.clear();
        b.shapes.reserve(shapes.size());
        for (ShapeId sid : shapes) {
            if (lookupShape(sid) == nullptr) {
                continue;
            }
            b.shapes.push_back(sid);
            incrementShapeRef(sid);
        }
        b.alive = true;
        return BodyId{makeHandle(slot, b.generation)};
    }

    void destroyBody(PhysicsWorldId world, BodyId body) override {
        WorldSlot* w = lookupWorld(world);
        if (w == nullptr) {
            return;
        }
        BodySlot* b = lookupBody(*w, body);
        if (b == nullptr) {
            return;
        }
        releaseBodyShapeRefs(*b);
        b->alive = false;
        ++b->generation;
    }

    std::optional<BodyState> getBodyState(PhysicsWorldId world,
                                          BodyId body) override {
        WorldSlot* w = lookupWorld(world);
        if (w == nullptr) {
            return std::nullopt;
        }
        const BodySlot* b = lookupBody(*w, body);
        if (b == nullptr) {
            return std::nullopt;
        }
        return b->state;
    }

    void setBodyTransform(PhysicsWorldId world,
                          BodyId body,
                          const Vec3& position,
                          const Quat& rotation) override {
        WorldSlot* w = lookupWorld(world);
        if (w == nullptr) {
            return;
        }
        BodySlot* b = lookupBody(*w, body);
        if (b == nullptr) {
            return;
        }
        // Mirror the teleport into both the create-time desc and the
        // live state so a subsequent `getBodyState` or syncBatch reads
        // the updated pose and a re-create-from-desc starts there too.
        b->desc.position = position;
        b->desc.rotation = rotation;
        b->state.position = position;
        b->state.rotation = rotation;
    }

    void stepWorld(PhysicsWorldId world, float dt) override {
        WorldSlot* w = lookupWorld(world);
        if (w == nullptr) {
            return;
        }
        // Kinematic-only integrator: every non-Static alive body
        // advances by velocity * dt. Velocities themselves are not
        // damped or accelerated — a real backend's gravity / forces /
        // collision response live in P9.
        for (BodySlot& b : w->bodies) {
            if (!b.alive || b.desc.type == BodyType::Static) {
                continue;
            }
            b.state.position = b.state.position + b.state.linearVelocity * dt;
            b.state.rotation = integrateAngular(
                b.state.rotation, b.state.angularVelocity, dt);
        }
    }

    void syncBodiesToGame(PhysicsWorldId world,
                          std::span<const BodyId> bodies,
                          std::span<BodyState> outStates) override {
        WorldSlot* w = lookupWorld(world);
        if (w == nullptr) {
            return;
        }
        if (bodies.size() != outStates.size()) {
            return;
        }
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            const BodySlot* b = lookupBody(*w, bodies[i]);
            if (b != nullptr) {
                outStates[i] = b->state;
            } else {
                outStates[i] = BodyState{};
            }
        }
    }

private:
    std::vector<WorldSlot> worlds_;
    std::vector<ShapeSlot> shapes_;

    std::uint32_t allocWorldSlot() {
        for (std::uint32_t i = 1; i < worlds_.size(); ++i) {
            if (!worlds_[i].alive) {
                ++worlds_[i].generation;
                return i;
            }
        }
        worlds_.emplace_back();
        WorldSlot& s = worlds_.back();
        s.generation = 1;
        return static_cast<std::uint32_t>(worlds_.size() - 1);
    }

    std::uint32_t allocShapeSlot() {
        for (std::uint32_t i = 1; i < shapes_.size(); ++i) {
            if (!shapes_[i].alive) {
                ++shapes_[i].generation;
                return i;
            }
        }
        shapes_.emplace_back();
        ShapeSlot& s = shapes_.back();
        s.generation = 1;
        return static_cast<std::uint32_t>(shapes_.size() - 1);
    }

    std::uint32_t allocBodySlot(WorldSlot& w) {
        for (std::uint32_t i = 1; i < w.bodies.size(); ++i) {
            if (!w.bodies[i].alive) {
                ++w.bodies[i].generation;
                return i;
            }
        }
        w.bodies.emplace_back();
        BodySlot& s = w.bodies.back();
        s.generation = 1;
        return static_cast<std::uint32_t>(w.bodies.size() - 1);
    }

    WorldSlot* lookupWorld(PhysicsWorldId world) {
        if (world.value == kInvalidSlot) {
            return nullptr;
        }
        const std::uint32_t slot = slotOf(world.value);
        if (slot == 0 || slot >= worlds_.size()) {
            return nullptr;
        }
        WorldSlot& w = worlds_[slot];
        if (!w.alive || w.generation != generationOf(world.value)) {
            return nullptr;
        }
        return &w;
    }

    BodySlot* lookupBody(WorldSlot& w, BodyId body) {
        if (body.value == kInvalidSlot) {
            return nullptr;
        }
        const std::uint32_t slot = slotOf(body.value);
        if (slot == 0 || slot >= w.bodies.size()) {
            return nullptr;
        }
        BodySlot& b = w.bodies[slot];
        if (!b.alive || b.generation != generationOf(body.value)) {
            return nullptr;
        }
        return &b;
    }

    ShapeSlot* lookupShape(ShapeId shape) {
        if (shape.value == kInvalidSlot) {
            return nullptr;
        }
        const std::uint32_t slot = slotOf(shape.value);
        if (slot == 0 || slot >= shapes_.size()) {
            return nullptr;
        }
        ShapeSlot& s = shapes_[slot];
        if (!s.alive || s.generation != generationOf(shape.value)) {
            return nullptr;
        }
        return &s;
    }

    void incrementShapeRef(ShapeId shape) {
        ShapeSlot* s = lookupShape(shape);
        if (s == nullptr) {
            return;
        }
        ++s->refCount;
    }

    void decrementShapeRef(ShapeId shape) {
        ShapeSlot* s = lookupShape(shape);
        if (s == nullptr || s->refCount == 0) {
            return;
        }
        --s->refCount;
        if (s->refCount == 0 && s->pendingDestroy) {
            freeShape(slotOf(shape.value));
        }
    }

    void releaseBodyShapeRefs(BodySlot& body) {
        for (ShapeId sid : body.shapes) {
            decrementShapeRef(sid);
        }
        body.shapes.clear();
    }

    // Actually free a shape slot — refCount must already be zero.
    // For Compound shapes, child refs are decremented BEFORE the
    // generation bump so cascaded `freeShape` calls see stable handles
    // and don't double-free.
    void freeShape(std::uint32_t slot) {
        ShapeSlot& s = shapes_[slot];
        if (!s.alive) {
            return;
        }
        if (s.desc.type == ShapeType::Compound) {
            // Snapshot the children — `decrementShapeRef` may cascade
            // into another `freeShape` which mutates `shapes_` and
            // could invalidate references into `s.desc.children` after
            // the parent is wiped.
            const std::vector<ShapeId> children = s.desc.children;
            for (ShapeId child : children) {
                decrementShapeRef(child);
            }
        }
        s.alive = false;
        s.refCount = 0;
        s.pendingDestroy = false;
        ++s.generation;
        // Wipe heavy mesh / hull data; cooked backends would mirror this.
        s.desc.vertices.clear();
        s.desc.indices.clear();
        s.desc.children.clear();
    }

    static ShapeAabb computeAabbPrimitive(const ShapeDesc& desc) {
        switch (desc.type) {
        case ShapeType::Box:
            return ShapeAabb{
                Vec3{-desc.halfExtents.x, -desc.halfExtents.y, -desc.halfExtents.z},
                Vec3{ desc.halfExtents.x,  desc.halfExtents.y,  desc.halfExtents.z}};
        case ShapeType::Sphere:
            return ShapeAabb{
                Vec3{-desc.radius, -desc.radius, -desc.radius},
                Vec3{ desc.radius,  desc.radius,  desc.radius}};
        case ShapeType::Capsule: {
            const float halfH = desc.height * 0.5f + desc.radius;
            return ShapeAabb{
                Vec3{-desc.radius, -halfH, -desc.radius},
                Vec3{ desc.radius,  halfH,  desc.radius}};
        }
        case ShapeType::ConvexHull:
        case ShapeType::Mesh: {
            if (desc.vertices.empty()) {
                return ShapeAabb{};
            }
            Vec3 mn = desc.vertices[0];
            Vec3 mx = desc.vertices[0];
            for (const Vec3& v : desc.vertices) {
                mn.x = std::min(mn.x, v.x);
                mn.y = std::min(mn.y, v.y);
                mn.z = std::min(mn.z, v.z);
                mx.x = std::max(mx.x, v.x);
                mx.y = std::max(mx.y, v.y);
                mx.z = std::max(mx.z, v.z);
            }
            return ShapeAabb{mn, mx};
        }
        case ShapeType::Compound:
            break; // handled by caller
        }
        return ShapeAabb{};
    }

    ShapeAabb computeAabb(const ShapeDesc& desc) {
        if (desc.type != ShapeType::Compound) {
            return computeAabbPrimitive(desc);
        }
        // Union of every still-alive child's AABB. Bounds are at the
        // origin — per-child local transforms are deferred (see
        // shape.hpp comment).
        ShapeAabb out{};
        bool first = true;
        for (ShapeId child : desc.children) {
            ShapeSlot* cs = lookupShape(child);
            if (cs == nullptr) {
                continue;
            }
            const ShapeAabb childAabb = computeAabb(cs->desc);
            if (first) {
                out = childAabb;
                first = false;
            } else {
                out.min.x = std::min(out.min.x, childAabb.min.x);
                out.min.y = std::min(out.min.y, childAabb.min.y);
                out.min.z = std::min(out.min.z, childAabb.min.z);
                out.max.x = std::max(out.max.x, childAabb.max.x);
                out.max.y = std::max(out.max.y, childAabb.max.y);
                out.max.z = std::max(out.max.z, childAabb.max.z);
            }
        }
        return out;
    }
};

} // namespace

std::unique_ptr<IPhysicsBackend> makeStubBackend() {
    return std::make_unique<StubBackend>();
}

} // namespace threadmaxx::physics
