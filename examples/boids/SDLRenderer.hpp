#pragma once

#include <threadmaxx/Renderer.hpp>

#include <cstdint>

struct SDL_Window;
struct SDL_Renderer;

namespace threadmaxx { class Engine; }

// Minimal SDL2 backend. Opens a window in initialize(), draws each
// RenderInstance as a small filled rect during submitFrame(), and pumps the
// event loop so the user can close the window. SDL_QUIT / Esc both forward to
// Engine::requestQuit() so the main loop terminates cleanly.
//
// The renderer borrows the Engine pointer for quit signaling only — it does
// not call any other engine method. The engine outlives this renderer.
class SDLRenderer : public threadmaxx::IRenderer {
public:
    explicit SDLRenderer(threadmaxx::Engine* engine) : engine_(engine) {}

    bool initialize() override;
    void shutdown() override;
    void submitFrame(const threadmaxx::RenderFrame& frame) override;

private:
    threadmaxx::Engine* engine_ = nullptr;
    SDL_Window*         window_ = nullptr;
    SDL_Renderer*       sdl_    = nullptr;
    std::uint64_t       framesSubmitted_ = 0;
};
