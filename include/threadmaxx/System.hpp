#pragma once

#include "CommandBuffer.hpp"

#include <cstdint>
#include <functional>

namespace threadmaxx {

class World;

// Half-open index range [begin, end) into the dense entity arrays.
struct Range {
    std::uint32_t begin = 0;
    std::uint32_t end = 0;

    constexpr std::uint32_t size() const noexcept { return end - begin; }
    constexpr bool empty()        const noexcept { return end <= begin; }
};

// Context handed to a system's update(). Systems schedule parallel work by
// calling parallelFor; the engine slices the range, hands each slice to a
// worker, and waits for completion before returning to the system.
class SystemContext {
public:
    using JobFn = std::function<void(Range, CommandBuffer&)>;

    virtual ~SystemContext() = default;

    virtual const World& world() const noexcept = 0;
    virtual double       dt()    const noexcept = 0;
    virtual std::uint64_t tick() const noexcept = 0;

    // Splits [0, count) into chunks of about `grain` items each, schedules
    // one job per chunk on the worker pool, and waits. Each job receives its
    // slice and a private CommandBuffer. Pass grain=0 to let the engine pick.
    virtual void parallelFor(std::uint32_t count,
                             std::uint32_t grain,
                             JobFn fn) = 0;

    // Run on the simulation thread, single-threaded. Useful for setup work
    // or systems that fundamentally cannot be parallelized.
    virtual void single(JobFn fn) = 0;
};

// Categories of state a system can declare it reads or writes. The engine
// uses these to schedule independent systems concurrently within a wave.
// `EntityStructural` covers spawn/destroy — any system that creates or
// kills entities must list it as a write (even if it doesn't touch the
// per-component arrays directly).
enum class Component : std::uint32_t {
    Transform        = 1u << 0,
    Velocity         = 1u << 1,
    RenderTag        = 1u << 2,
    UserData         = 1u << 3,
    EntityStructural = 1u << 4,
};

// Bitset over Component values. Trivially copyable, no allocation.
class ComponentSet {
public:
    constexpr ComponentSet() noexcept = default;
    constexpr ComponentSet(Component c) noexcept
        : bits_(static_cast<std::uint32_t>(c)) {}
    constexpr ComponentSet(std::initializer_list<Component> cs) noexcept {
        for (auto c : cs) bits_ |= static_cast<std::uint32_t>(c);
    }

    // The set containing every Component.
    static constexpr ComponentSet all() noexcept {
        ComponentSet s;
        s.bits_ = 0x1Fu;  // bits 0..4 — keep in sync with Component
        return s;
    }
    static constexpr ComponentSet none() noexcept { return ComponentSet{}; }

    constexpr std::uint32_t bits()  const noexcept { return bits_; }
    constexpr bool          empty() const noexcept { return bits_ == 0; }
    constexpr bool intersects(ComponentSet o) const noexcept {
        return (bits_ & o.bits_) != 0;
    }

    constexpr ComponentSet operator|(ComponentSet o) const noexcept {
        ComponentSet r; r.bits_ = bits_ | o.bits_; return r;
    }
    constexpr ComponentSet& operator|=(ComponentSet o) noexcept {
        bits_ |= o.bits_; return *this;
    }
    constexpr bool operator==(const ComponentSet&) const noexcept = default;

private:
    std::uint32_t bits_ = 0;
};

constexpr ComponentSet operator|(Component a, Component b) noexcept {
    return ComponentSet{a} | ComponentSet{b};
}

// User-implemented unit of gameplay. Stateless or with internal state owned
// by the implementation. Registered with the engine via IGame.
class ISystem {
public:
    virtual ~ISystem() = default;

    virtual const char* name() const noexcept = 0;

    // Called once after the engine has constructed the world.
    virtual void onRegister(World&) {}

    // Called once at engine shutdown, before the world is torn down.
    virtual void onUnregister(World&) {}

    // Called every fixed step. Use ctx.parallelFor / ctx.single to do work.
    virtual void update(SystemContext& ctx) = 0;

    // Read/write sets the engine consults when deciding which systems can
    // run concurrently within a wave. Two systems S1 and S2 conflict iff
    //   S1.writes ∩ S2.writes ≠ ∅, or
    //   S1.writes ∩ S2.reads  ≠ ∅, or
    //   S1.reads  ∩ S2.writes ≠ ∅
    // Defaults are the universal set, which makes every pair conflict and
    // forces strict sequential execution in registration order. Override
    // these to opt in to parallel scheduling.
    virtual ComponentSet reads()  const noexcept { return ComponentSet::all(); }
    virtual ComponentSet writes() const noexcept { return ComponentSet::all(); }
};

} // namespace threadmaxx
