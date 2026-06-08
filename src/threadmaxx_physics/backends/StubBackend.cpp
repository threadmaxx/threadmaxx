#include "threadmaxx_physics/stub_backend.hpp"

#include "threadmaxx_physics/constraints.hpp"
#include "threadmaxx_physics/contact.hpp"
#include "threadmaxx_physics/query.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
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
// P4 wired `stepWorld` to a kinematic-only integrator: every alive
// non-Static body advances by `linearVelocity * dt` and composes a
// rotation built from `angularVelocity * dt` interpreted as world-frame
// axis-angle. No collision, no gravity, no forces — those belong to
// the real backend (P9). Static bodies are skipped.
//
// P5 (this file) adds three synchronous queries against the kinematic
// body positions: closest-hit raycast, closest-hit sphere sweep, and
// sphere overlap. The stub answers all three against a per-body
// world-space AABB — union of every attached shape's local-space AABB,
// translated by the body's current position. Rotation is ignored at
// the stub; real backends do the proper OBB/narrowphase.
//
// P6 (this file) adds the constraint (joint) registry. Constraints are
// scoped per-world (handles encode (generation, slot) the same way as
// body / world / shape ids); the stub records the descriptor verbatim
// but does NOT enforce the geometric relationship at `stepWorld` (no
// solver). Destroying either coupled body invalidates the constraint —
// `getConstraint` returns nullopt without the host having to call
// `destroyConstraint` first. Matches the real-backend contract:
// solver-owned constraints disappear when one of their bodies is
// removed, even when host code forgets to release the handle. The
// stub-side detection runs in `destroyBody` (sweep the constraint
// table, mark every constraint touching the dying body as dead).
//
// P8 (this file) adds contact event detection. The world keeps a sorted
// vector of currently-overlapping body pairs (canonicalized so
// `pair.lo.value < pair.hi.value`). Every `stepWorld` recomputes the
// pair set after integration via an all-pairs AABB overlap test and
// diffs against the previous tick: pairs new this tick fire Begin,
// pairs gone this tick fire End. Pairs present in both ticks are
// silently kept (no Persist phase — game code that wants per-tick
// contact processing can run `overlapBodies` on its own schedule).
// `destroyBody` emits End events for every active pair touching the
// dying body BEFORE bumping the generation, then removes those pairs
// so the next `stepWorld` diff sees no spurious End. Layer filtering
// is intentionally NOT applied at the stub — real backends use their
// layer-pair matrix, and game code that needs layer awareness at the
// stub can filter inside the callback.

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

// Layer filter helper: a body sits in layer N (single index 0..31);
// queries pass a 32-bit mask. Body is considered iff the corresponding
// bit is set in the mask. Out-of-range layer indices are clamped to
// match no bits.
constexpr bool layerMatches(std::uint32_t bodyLayer,
                            std::uint32_t mask) noexcept {
    if (bodyLayer >= 32u) {
        return false;
    }
    return ((1u << bodyLayer) & mask) != 0u;
}

// Slab-method ray vs AABB. `aabb` is world-space; returns the entry
// distance and which axis (0=X, 1=Y, 2=Z) produced it, plus the sign of
// the face normal pointing back along the ray. Returns nullopt on miss
// (entry > exit) or when the hit lies past `maxDistance` / behind the
// origin.
//
// Parallel-axis case (|direction[i]| < eps): the ray must already lie
// in the slab on that axis, otherwise miss.
struct RayAabbHit {
    float distance{};
    int axis{};        // 0..2
    float normalSign{}; // -1 means hit min face, +1 means hit max face
};

inline std::optional<RayAabbHit> rayVsAabb(const Vec3& origin,
                                           const Vec3& direction,
                                           float maxDistance,
                                           const ShapeAabb& aabb) noexcept {
    constexpr float kEps = 1e-7f;
    float tNear = -std::numeric_limits<float>::infinity();
    float tFar  =  std::numeric_limits<float>::infinity();
    int   nearAxis = 0;
    float nearSign = -1.0f;

    const float o[3] = {origin.x, origin.y, origin.z};
    const float d[3] = {direction.x, direction.y, direction.z};
    const float mn[3] = {aabb.min.x, aabb.min.y, aabb.min.z};
    const float mx[3] = {aabb.max.x, aabb.max.y, aabb.max.z};

    for (int i = 0; i < 3; ++i) {
        if (std::fabs(d[i]) < kEps) {
            // Parallel: must lie inside the slab on this axis.
            if (o[i] < mn[i] || o[i] > mx[i]) {
                return std::nullopt;
            }
            continue;
        }
        float t1 = (mn[i] - o[i]) / d[i];
        float t2 = (mx[i] - o[i]) / d[i];
        // Track which face the entry came from before any swap so we
        // can read the normal off the surviving plane.
        float sign = -1.0f; // hit the min face
        if (t1 > t2) {
            std::swap(t1, t2);
            sign = +1.0f;   // post-swap, the entry is the max face
        }
        if (t1 > tNear) {
            tNear = t1;
            nearAxis = i;
            nearSign = sign;
        }
        if (t2 < tFar) {
            tFar = t2;
        }
        if (tNear > tFar) {
            return std::nullopt;
        }
    }

    // Hit must be in front of the ray and within the requested length.
    // Origin-inside (tNear < 0) is treated as a miss for raycast — the
    // caller wants entry from outside.
    if (tNear < 0.0f || tNear > maxDistance) {
        return std::nullopt;
    }
    return RayAabbHit{tNear, nearAxis, nearSign};
}

// Test whether two world-space AABBs overlap (inclusive on the
// boundary — touching faces count as overlap). Used by P8 contact
// detection.
constexpr bool aabbsOverlap(const ShapeAabb& a, const ShapeAabb& b) noexcept {
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

// Canonical contact pair (lo.value < hi.value) with lex ordering on
// (lo, hi). Used to maintain the world's sorted active-pair set for
// O(n) Begin/End diffing.
struct ContactPair {
    BodyId lo{};
    BodyId hi{};
};

constexpr bool operator<(const ContactPair& a, const ContactPair& b) noexcept {
    if (a.lo.value != b.lo.value) return a.lo.value < b.lo.value;
    return a.hi.value < b.hi.value;
}

// Distance from `point` to the closest point on `aabb`. Zero when the
// point lies inside. Used by overlap and sphere-vs-AABB queries.
inline float pointAabbDistanceSq(const Vec3& point, const ShapeAabb& aabb) noexcept {
    float dx = 0.0f;
    if (point.x < aabb.min.x) dx = aabb.min.x - point.x;
    else if (point.x > aabb.max.x) dx = point.x - aabb.max.x;
    float dy = 0.0f;
    if (point.y < aabb.min.y) dy = aabb.min.y - point.y;
    else if (point.y > aabb.max.y) dy = point.y - aabb.max.y;
    float dz = 0.0f;
    if (point.z < aabb.min.z) dz = aabb.min.z - point.z;
    else if (point.z > aabb.max.z) dz = point.z - aabb.max.z;
    return dx*dx + dy*dy + dz*dz;
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

struct ConstraintSlot {
    ConstraintDesc desc{};
    std::uint32_t generation{};
    bool alive{};
};

struct WorldSlot {
    PhysicsConfig config{};
    std::vector<BodySlot> bodies;
    std::vector<ConstraintSlot> constraints;
    // P8 contact detection: callback installed via setContactCallback
    // and the sorted vector of currently-overlapping pairs from the
    // previous `stepWorld`. Both are reset by destroyWorld and lazily
    // re-grown across world reuse.
    ContactCallback contactCallback{};
    std::vector<ContactPair> activeContacts{};
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
        s.constraints.clear();
        s.constraints.emplace_back(); // reserve joint slot 0
        s.contactCallback = ContactCallback{};
        s.activeContacts.clear();
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
        w->constraints.clear();
        w->contactCallback = ContactCallback{};
        w->activeContacts.clear();
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
        // P8: emit End events for every active contact pair touching
        // the dying body BEFORE the generation bump so the callback
        // observes the body's pre-destroy `BodyId`. Then drop those
        // pairs from `activeContacts` so the next stepWorld diff
        // doesn't fire a redundant End.
        if (!w->activeContacts.empty()) {
            auto pairTouchesBody = [body](const ContactPair& p) noexcept {
                return p.lo == body || p.hi == body;
            };
            if (w->contactCallback) {
                for (const ContactPair& p : w->activeContacts) {
                    if (pairTouchesBody(p)) {
                        ContactEvent ev{ContactPhase::End, p.lo, p.hi};
                        w->contactCallback(ev);
                    }
                }
            }
            w->activeContacts.erase(
                std::remove_if(w->activeContacts.begin(),
                               w->activeContacts.end(),
                               pairTouchesBody),
                w->activeContacts.end());
        }
        releaseBodyShapeRefs(*b);
        b->alive = false;
        ++b->generation;
        // Any constraint that referenced this body is now stale —
        // match the real-backend contract: solver-owned constraints
        // disappear when one of their bodies is removed, even when
        // host code forgets to release the handle. We mark the
        // constraint slots dead in place (generation bump so any held
        // `JointId` returns nullopt on subsequent lookups).
        for (ConstraintSlot& c : w->constraints) {
            if (!c.alive) {
                continue;
            }
            if (c.desc.bodyA == body || c.desc.bodyB == body) {
                c.alive = false;
                ++c.generation;
            }
        }
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
        detectContacts(*w);
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

    std::optional<RaycastHit> raycast(PhysicsWorldId world,
                                      const RaycastRequest& request) override {
        WorldSlot* w = lookupWorld(world);
        if (w == nullptr) {
            return std::nullopt;
        }

        float bestT = std::numeric_limits<float>::infinity();
        std::uint32_t bestSlot = 0;
        int bestAxis = 0;
        float bestSign = -1.0f;

        for (std::uint32_t i = 1; i < w->bodies.size(); ++i) {
            BodySlot& b = w->bodies[i];
            if (!b.alive) {
                continue;
            }
            if (!layerMatches(b.desc.layer, request.layerMask)) {
                continue;
            }
            ShapeAabb aabb = computeBodyWorldAabb(b);
            auto hit = rayVsAabb(request.origin, request.direction,
                                 request.maxDistance, aabb);
            if (!hit.has_value()) {
                continue;
            }
            if (hit->distance < bestT) {
                bestT = hit->distance;
                bestSlot = i;
                bestAxis = hit->axis;
                bestSign = hit->normalSign;
            }
        }

        if (bestSlot == 0) {
            return std::nullopt;
        }

        RaycastHit out{};
        out.body = BodyId{makeHandle(bestSlot, w->bodies[bestSlot].generation)};
        out.distance = bestT;
        out.position = request.origin + request.direction * bestT;
        // Normal points back along the ray on the entry-face axis.
        Vec3 n{};
        if (bestAxis == 0) n.x = bestSign;
        else if (bestAxis == 1) n.y = bestSign;
        else n.z = bestSign;
        out.normal = n;
        return out;
    }

    std::optional<SweepHit> sweep(PhysicsWorldId world,
                                  const SweepRequest& request) override {
        WorldSlot* w = lookupWorld(world);
        if (w == nullptr) {
            return std::nullopt;
        }
        if (request.radius < 0.0f) {
            return std::nullopt;
        }

        float bestT = std::numeric_limits<float>::infinity();
        std::uint32_t bestSlot = 0;
        int bestAxis = 0;
        float bestSign = -1.0f;

        for (std::uint32_t i = 1; i < w->bodies.size(); ++i) {
            BodySlot& b = w->bodies[i];
            if (!b.alive) {
                continue;
            }
            if (!layerMatches(b.desc.layer, request.layerMask)) {
                continue;
            }
            // Minkowski-sum a sphere of `radius` with the AABB by
            // inflating each axis. Real backends do the proper round-
            // corner GJK / capsule shrink; the stub's box approximation
            // is conservative (overestimates hits at corners).
            ShapeAabb aabb = computeBodyWorldAabb(b);
            const float r = request.radius;
            aabb.min.x -= r; aabb.min.y -= r; aabb.min.z -= r;
            aabb.max.x += r; aabb.max.y += r; aabb.max.z += r;
            auto hit = rayVsAabb(request.start, request.direction,
                                 request.maxDistance, aabb);
            if (!hit.has_value()) {
                continue;
            }
            if (hit->distance < bestT) {
                bestT = hit->distance;
                bestSlot = i;
                bestAxis = hit->axis;
                bestSign = hit->normalSign;
            }
        }

        if (bestSlot == 0) {
            return std::nullopt;
        }

        SweepHit out{};
        out.body = BodyId{makeHandle(bestSlot, w->bodies[bestSlot].generation)};
        out.distance = bestT;
        out.position = request.start + request.direction * bestT;
        Vec3 n{};
        if (bestAxis == 0) n.x = bestSign;
        else if (bestAxis == 1) n.y = bestSign;
        else n.z = bestSign;
        out.normal = n;
        return out;
    }

    JointId createConstraint(PhysicsWorldId world,
                             const ConstraintDesc& desc) override {
        WorldSlot* w = lookupWorld(world);
        if (w == nullptr) {
            return JointId{};
        }
        // Reject self-constraints and stale body refs — a constraint
        // between a body and itself is meaningless, and a stale body id
        // would silently create a dead-on-arrival constraint.
        if (desc.bodyA == desc.bodyB) {
            return JointId{};
        }
        if (lookupBody(*w, desc.bodyA) == nullptr ||
            lookupBody(*w, desc.bodyB) == nullptr) {
            return JointId{};
        }
        const auto slot = allocConstraintSlot(*w);
        ConstraintSlot& c = w->constraints[slot];
        c.desc = desc;
        c.alive = true;
        return JointId{makeHandle(slot, c.generation)};
    }

    void destroyConstraint(PhysicsWorldId world, JointId joint) override {
        WorldSlot* w = lookupWorld(world);
        if (w == nullptr) {
            return;
        }
        ConstraintSlot* c = lookupConstraint(*w, joint);
        if (c == nullptr) {
            return;
        }
        c->alive = false;
        ++c->generation;
    }

    std::optional<ConstraintDesc> getConstraint(PhysicsWorldId world,
                                                JointId joint) override {
        WorldSlot* w = lookupWorld(world);
        if (w == nullptr) {
            return std::nullopt;
        }
        const ConstraintSlot* c = lookupConstraint(*w, joint);
        if (c == nullptr) {
            return std::nullopt;
        }
        return c->desc;
    }

    void setContactCallback(PhysicsWorldId world,
                            ContactCallback callback) override {
        WorldSlot* w = lookupWorld(world);
        if (w == nullptr) {
            return;
        }
        w->contactCallback = std::move(callback);
    }

    void overlap(PhysicsWorldId world,
                 const OverlapRequest& request,
                 std::vector<BodyId>& outBodies) override {
        outBodies.clear();
        WorldSlot* w = lookupWorld(world);
        if (w == nullptr) {
            return;
        }
        const float radSq = request.radius * request.radius;
        for (std::uint32_t i = 1; i < w->bodies.size(); ++i) {
            BodySlot& b = w->bodies[i];
            if (!b.alive) {
                continue;
            }
            if (!layerMatches(b.desc.layer, request.layerMask)) {
                continue;
            }
            const ShapeAabb aabb = computeBodyWorldAabb(b);
            const float dSq = pointAabbDistanceSq(request.center, aabb);
            // dSq == 0 means the center lies inside the AABB; any
            // non-zero `radius` strictly outside still hits when the
            // closest-point distance falls within. radius=0 collapses
            // to "is the center inside?".
            if (dSq <= radSq) {
                outBodies.push_back(
                    BodyId{makeHandle(i, b.generation)});
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

    std::uint32_t allocConstraintSlot(WorldSlot& w) {
        for (std::uint32_t i = 1; i < w.constraints.size(); ++i) {
            if (!w.constraints[i].alive) {
                ++w.constraints[i].generation;
                return i;
            }
        }
        w.constraints.emplace_back();
        ConstraintSlot& s = w.constraints.back();
        s.generation = 1;
        return static_cast<std::uint32_t>(w.constraints.size() - 1);
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

    ConstraintSlot* lookupConstraint(WorldSlot& w, JointId joint) {
        if (joint.value == kInvalidSlot) {
            return nullptr;
        }
        const std::uint32_t slot = slotOf(joint.value);
        if (slot == 0 || slot >= w.constraints.size()) {
            return nullptr;
        }
        ConstraintSlot& c = w.constraints[slot];
        if (!c.alive || c.generation != generationOf(joint.value)) {
            return nullptr;
        }
        return &c;
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

    // World-space AABB of a body: union of every attached shape's
    // local-space AABB, translated by the body's current position.
    // Rotation is intentionally ignored at the stub (no OBB / true
    // narrowphase); real backends do that work in P9. Bodies with no
    // shapes degenerate to a zero-extent AABB at `state.position`,
    // which still answers point-overlap and ray-hits-the-point.
    ShapeAabb computeBodyWorldAabb(const BodySlot& b) {
        ShapeAabb out{};
        bool any = false;
        for (ShapeId sid : b.shapes) {
            ShapeSlot* s = lookupShape(sid);
            if (s == nullptr) {
                continue;
            }
            const ShapeAabb sa = computeAabb(s->desc);
            if (!any) {
                out = sa;
                any = true;
            } else {
                out.min.x = std::min(out.min.x, sa.min.x);
                out.min.y = std::min(out.min.y, sa.min.y);
                out.min.z = std::min(out.min.z, sa.min.z);
                out.max.x = std::max(out.max.x, sa.max.x);
                out.max.y = std::max(out.max.y, sa.max.y);
                out.max.z = std::max(out.max.z, sa.max.z);
            }
        }
        if (!any) {
            out.min = b.state.position;
            out.max = b.state.position;
            return out;
        }
        out.min = out.min + b.state.position;
        out.max = out.max + b.state.position;
        return out;
    }

    // P8 contact detection: all-pairs AABB overlap test against the
    // post-integration body positions, diffed against the prior tick's
    // sorted active set. Begin fires for new pairs, End for departed
    // pairs; persisting pairs are silently kept.
    //
    // Complexity is O(n²) over alive bodies — fine for the stub at
    // gameplay-test cardinalities and the only path that doesn't need
    // a broadphase. Real backends (P9) plug into the solver's own
    // pair iterator.
    void detectContacts(WorldSlot& w) {
        std::vector<ContactPair> next;
        // Precompute world AABBs once per body so the inner loop pays
        // for it `n` times instead of `n²`.
        const std::size_t bodyCount = w.bodies.size();
        std::vector<ShapeAabb> aabbCache;
        std::vector<BodyId> idCache;
        aabbCache.reserve(bodyCount);
        idCache.reserve(bodyCount);
        for (std::uint32_t i = 0; i < bodyCount; ++i) {
            const BodySlot& b = w.bodies[i];
            if (!b.alive) {
                aabbCache.emplace_back();
                idCache.emplace_back();
                continue;
            }
            aabbCache.push_back(computeBodyWorldAabb(b));
            idCache.push_back(BodyId{makeHandle(i, b.generation)});
        }
        for (std::uint32_t i = 1; i < bodyCount; ++i) {
            if (!w.bodies[i].alive) continue;
            for (std::uint32_t j = i + 1; j < bodyCount; ++j) {
                if (!w.bodies[j].alive) continue;
                if (!aabbsOverlap(aabbCache[i], aabbCache[j])) continue;
                ContactPair pair{idCache[i], idCache[j]};
                if (pair.hi.value < pair.lo.value) {
                    std::swap(pair.lo, pair.hi);
                }
                next.push_back(pair);
            }
        }
        std::sort(next.begin(), next.end());
        // Sorted merge-diff against the prior tick's active pairs:
        // emit Begin for entries in `next` not in `activeContacts`,
        // emit End for entries in `activeContacts` not in `next`.
        const auto& prev = w.activeContacts;
        std::size_t pi = 0;
        std::size_t ni = 0;
        const ContactCallback& cb = w.contactCallback;
        while (pi < prev.size() && ni < next.size()) {
            if (prev[pi] < next[ni]) {
                if (cb) {
                    ContactEvent ev{ContactPhase::End, prev[pi].lo, prev[pi].hi};
                    cb(ev);
                }
                ++pi;
            } else if (next[ni] < prev[pi]) {
                if (cb) {
                    ContactEvent ev{ContactPhase::Begin, next[ni].lo, next[ni].hi};
                    cb(ev);
                }
                ++ni;
            } else {
                ++pi;
                ++ni;
            }
        }
        while (pi < prev.size()) {
            if (cb) {
                ContactEvent ev{ContactPhase::End, prev[pi].lo, prev[pi].hi};
                cb(ev);
            }
            ++pi;
        }
        while (ni < next.size()) {
            if (cb) {
                ContactEvent ev{ContactPhase::Begin, next[ni].lo, next[ni].hi};
                cb(ev);
            }
            ++ni;
        }
        w.activeContacts = std::move(next);
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
