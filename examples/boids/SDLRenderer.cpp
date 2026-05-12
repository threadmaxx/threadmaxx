#include "SDLRenderer.hpp"

#include "BoidsConfig.hpp"

#include <threadmaxx/Engine.hpp>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstdio>

bool SDLRenderer::initialize() {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    window_ = SDL_CreateWindow("threadmaxx — boids",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               static_cast<int>(boids::kWindowW),
                               static_cast<int>(boids::kWindowH),
                               SDL_WINDOW_SHOWN);
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }
    sdl_ = SDL_CreateRenderer(window_, -1,
                              SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl_) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window_);
        SDL_Quit();
        return false;
    }
    std::printf("[SDLRenderer] window %dx%d ready\n",
                static_cast<int>(boids::kWindowW), static_cast<int>(boids::kWindowH));
    return true;
}

void SDLRenderer::shutdown() {
    if (sdl_)    SDL_DestroyRenderer(sdl_);
    if (window_) SDL_DestroyWindow(window_);
    sdl_    = nullptr;
    window_ = nullptr;
    SDL_Quit();
    std::printf("[SDLRenderer] shutdown after %llu frames\n",
                static_cast<unsigned long long>(framesSubmitted_));
}

void SDLRenderer::submitFrame(const threadmaxx::RenderFrame& frame) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev) != 0) {
        if (ev.type == SDL_QUIT ||
            (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) {
            if (engine_) engine_->requestQuit();
        }
    }

    SDL_SetRenderDrawColor(sdl_, 12, 14, 22, 255);
    SDL_RenderClear(sdl_);

    SDL_SetRenderDrawColor(sdl_, 230, 230, 235, 255);
    for (const auto& inst : frame.instances) {
        SDL_FRect r{inst.transform.position.x - 1.5f,
                    inst.transform.position.z - 1.5f,
                    3.0f, 3.0f};
        SDL_RenderFillRectF(sdl_, &r);
    }

    SDL_RenderPresent(sdl_);
    ++framesSubmitted_;
}
