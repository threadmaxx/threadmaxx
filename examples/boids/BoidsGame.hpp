#pragma once

#include <threadmaxx/Game.hpp>

#include "SDLRenderer.hpp"

#include <memory>

class BoidsGame : public threadmaxx::IGame {
public:
    explicit BoidsGame(threadmaxx::Engine* engine) : engine_(engine) {}

    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World& world,
                 threadmaxx::CommandBuffer& seed) override;

    SDLRenderer* renderer() noexcept { return renderer_.get(); }

private:
    threadmaxx::Engine*          engine_ = nullptr;
    std::unique_ptr<SDLRenderer> renderer_;
};
