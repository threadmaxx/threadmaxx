#pragma once

#include "CommandBuffer.hpp"

namespace threadmaxx {

class Engine;
class World;

/// Game-side registration interface.
///
/// The engine constructs the world, then calls `onSetup()` to give the
/// game a chance to register its systems and renderer, seed the world
/// with initial entities, etc. After that the engine runs the
/// simulation loop until `requestQuit()` is called.
class IGame {
public:
    virtual ~IGame() = default;

    /// Register systems and renderer here via `engine.registerSystem(...)`
    /// / `engine.setRenderer(...)`. The `seed` buffer is committed
    /// before the first tick — use it to spawn starting entities.
    virtual void onSetup(Engine& engine, World& world, CommandBuffer& seed) = 0;

    /// Called once before shutdown, after the last tick.
    virtual void onTeardown(Engine& /*engine*/, World& /*world*/) {}
};

} // namespace threadmaxx
