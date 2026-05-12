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
};

} // namespace threadmaxx
