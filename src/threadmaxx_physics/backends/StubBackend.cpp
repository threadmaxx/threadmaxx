#include "threadmaxx_physics/stub_backend.hpp"

#include <cstdint>
#include <vector>

// StubBackend — deterministic, no-real-physics IPhysicsBackend.
//
// Worlds, shapes, and bodies live in flat slot tables guarded by
// (generation, alive) so that stale ids never alias newly-allocated
// slots. Ids encode `(slot << 32) | generation`; slot 0 is reserved
// for "invalid" so a default-constructed `PhysicsWorldId{}` is
// distinguishable from any live world.
//
// P1 ships the table machinery + create / destroy lifecycle plumbing.
// `stepWorld` is a no-op for P1 — the kinematic integrator lands in
// P4 (positions advance by `linearVelocity * dt`).

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

struct ShapeSlot {
    ShapeDesc desc{};
    std::uint32_t generation{};
    bool alive{};
};

struct BodySlot {
    BodyDesc desc{};
    BodyState state{};
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
        w->bodies.clear();
        w->alive = false;
        ++w->generation;
    }

    ShapeId createShape(const ShapeDesc& desc) override {
        const auto slot = allocShapeSlot();
        ShapeSlot& s = shapes_[slot];
        s.desc = desc;
        s.alive = true;
        return ShapeId{makeHandle(slot, s.generation)};
    }

    void destroyShape(ShapeId shape) override {
        if (shape.value == kInvalidSlot) {
            return;
        }
        const std::uint32_t slot = slotOf(shape.value);
        if (slot >= shapes_.size()) {
            return;
        }
        ShapeSlot& s = shapes_[slot];
        if (!s.alive || s.generation != generationOf(shape.value)) {
            return;
        }
        s.alive = false;
        ++s.generation;
        // Wipe heavy mesh / hull data; cooked backends would mirror this.
        s.desc.vertices.clear();
        s.desc.indices.clear();
    }

    BodyId createBody(PhysicsWorldId world,
                      const BodyDesc& desc,
                      std::span<const ShapeId> /*shapes*/) override {
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
        b->alive = false;
        ++b->generation;
    }

    void stepWorld(PhysicsWorldId world, float /*dt*/) override {
        // P1: stepWorld is a no-op by design. P4 will integrate
        // kinematic velocity into position. We still verify the world
        // is valid so callers see the same lifecycle behavior as a
        // real backend would.
        (void)lookupWorld(world);
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
};

} // namespace

std::unique_ptr<IPhysicsBackend> makeStubBackend() {
    return std::make_unique<StubBackend>();
}

} // namespace threadmaxx::physics
