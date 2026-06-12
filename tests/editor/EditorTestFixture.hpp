#pragma once

/// @file EditorTestFixture.hpp
/// @brief Shared editor-test scaffolding — a no-op `IGame` and a
/// `ScopedEngine` RAII that calls initialize/shutdown for us. The
/// editor tests don't simulate; they just need a valid Engine to
/// take a reference to.

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/World.hpp>

namespace threadmaxx::editor::test {

struct NoopGame final : threadmaxx::IGame {
    void onSetup(threadmaxx::Engine&,
                 threadmaxx::World&,
                 threadmaxx::CommandBuffer&) override {}
};

/// @brief RAII engine for editor tests. Constructs, initializes with a
/// no-op game, and shuts down on destruction.
class ScopedEngine {
public:
    explicit ScopedEngine(const threadmaxx::Config& cfg = {})
        : engine_(cfg) {
        engine_.initialize(game_);
    }
    ~ScopedEngine() { engine_.shutdown(); }

    ScopedEngine(const ScopedEngine&) = delete;
    ScopedEngine& operator=(const ScopedEngine&) = delete;

    threadmaxx::Engine&       engine()       noexcept { return engine_; }
    const threadmaxx::Engine& engine() const noexcept { return engine_; }

private:
    NoopGame game_{};
    threadmaxx::Engine engine_;
};

} // namespace threadmaxx::editor::test
