#pragma once

#include <threadmaxx/Game.hpp>

#include "ConsoleRenderer.hpp"

#include <memory>

// Bundles the example's startup choices: registers two systems, hooks up a
// ConsoleRenderer, and seeds a couple of initial entities.
class MyGame : public threadmaxx::IGame {
public:
    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World& world,
                 threadmaxx::CommandBuffer& seed) override;

    void onTeardown(threadmaxx::Engine& engine,
                    threadmaxx::World& world) override;

private:
    std::unique_ptr<ConsoleRenderer> renderer_;
};
