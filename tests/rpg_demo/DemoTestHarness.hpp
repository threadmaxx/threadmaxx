// §3.11 batch D-audit — shared test harness for the rpg_demo suite.
//
// Boots a headless Engine + DemoGame and exposes small helpers for
// driving the demo's systems without a window. GLFW callbacks are
// never installed; tests poke the global InputState directly.

#pragma once

#include "Check.hpp"

#include <DemoGame.hpp>
#include <DemoTypes.hpp>
#include <Input.hpp>

#include <threadmaxx/Config.hpp>
#include <threadmaxx/Engine.hpp>

#include <atomic>
#include <cstdint>
#include <memory>

namespace rpg::testing {

/// Field order matters: `engine` must destruct BEFORE `game` so the
/// engine's `~EngineImpl::shutdown()` (which calls `game_->onTeardown`)
/// sees a live game. Members destruct in reverse declaration order →
/// declare `game` first so it outlives the engine.
struct HeadlessFixture {
    std::unique_ptr<rpg::DemoGame>      game;
    std::unique_ptr<threadmaxx::Engine> engine;
};

/// Boot a fresh Engine with the shipped DemoGame.
inline HeadlessFixture makeHeadless() {
    threadmaxx::Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;
    HeadlessFixture fx;
    fx.game   = std::make_unique<rpg::DemoGame>();
    fx.engine = std::make_unique<threadmaxx::Engine>(cfg);
    CHECK(fx.engine->initialize(*fx.game));
    return fx;
}

/// Atomically set one edge bit on the global InputState as if a key
/// was pressed this frame. Cleared by whichever system consumes it.
inline void injectEdge(std::uint32_t bit) {
    rpg::input().edges.fetch_or(bit, std::memory_order_release);
}

/// Clear ALL edge bits — handy at the start of each test so prior
/// tests in the same process don't leak input state.
inline void resetEdges() {
    rpg::input().edges.store(0, std::memory_order_release);
}

} // namespace rpg::testing
