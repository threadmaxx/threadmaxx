// 3D RPG demo — §3.2 batch 10 main entry. Wires GLFW + Vulkan
// renderer + threadmaxx engine + the DemoGame.
//
// Controls:
//   W / A / S / D   move
//   Arrow keys      yaw / pitch the camera
//   Q / E           zoom in / out
//   F1              toggle Chrome-trace capture (/tmp/rpg_demo_trace.*.json)
//   F5              synchronous quick-save (built-ins + user comps)
//   F8              asynchronous quick-save (off-thread file write)
//   F9              load and restore from the saved file
//   F               sword swing (combat — batch D1)
//   Esc / window-close exits cleanly.
//
// CLI: `[--stress|-s] [tick_count]`
//   --stress / -s  — §3.11.5 batch D5: scale up to 10k NPCs + 50k
//                    pickups AND enable the engine's tick-budget
//                    skip policy (16.67ms/tick). HudSystem,
//                    DebugOverlaySystem, and DayNightSystem are
//                    `skippable()`; the brain bails early via
//                    `ctx.shouldYield()`.
//   tick_count     — integer cap (default 0 = unbounded).

#include "DemoGame.hpp"
#include "Input.hpp"

#include <threadmaxx_vk/VulkanRenderer.hpp>

#include <threadmaxx/Config.hpp>
#include <threadmaxx/Engine.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <thread>

namespace {

constexpr std::uint32_t kWidth  = 1280;
constexpr std::uint32_t kHeight = 720;

struct WindowUserData {
    threadmaxx::Engine* engine = nullptr;
    rpg::WorldState*    state  = nullptr;
};

void framebufferSizeCallback(GLFWwindow* win, int width, int height) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(win));
    if (!ud || width <= 0 || height <= 0) return;
    const auto w = static_cast<std::uint32_t>(width);
    const auto h = static_cast<std::uint32_t>(height);
    if (ud->state) {
        ud->state->framebufferWidth  = w;
        ud->state->framebufferHeight = h;
    }
    if (ud->engine) ud->engine->notifyResize(w, h);
}

} // namespace

int main(int argc, char** argv) {
    std::uint64_t maxTicks   = 0;
    bool          stressMode = false;
    // §3.11.5 batch D5 — `--stress` flag enables the 10k NPC + 50k
    // pickup scene + the engine's tick-budget skip policy. Any
    // remaining numeric arg is the tick cap (0 = unbounded).
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--stress") { stressMode = true; continue; }
        if (a == "-s")       { stressMode = true; continue; }
        maxTicks = static_cast<std::uint64_t>(std::strtoull(argv[i], nullptr, 10));
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "[rpg_demo] glfwInit failed\n");
        return 1;
    }
    if (!glfwVulkanSupported()) {
        std::fprintf(stderr, "[rpg_demo] Vulkan loader not available\n");
        glfwTerminate();
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(static_cast<int>(kWidth),
                                          static_cast<int>(kHeight),
                                          "threadmaxx — rpg_demo",
                                          nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[rpg_demo] glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }

    threadmaxx::Config cfg;
    cfg.workerCount = 0;
    threadmaxx::Engine engine(cfg);

    rpg::DemoGame game;
    game.worldState().framebufferWidth  = kWidth;
    game.worldState().framebufferHeight = kHeight;
    // §3.11.5 batch D5 — propagate the CLI flag into the game's
    // setup so DemoGame::onSetup uses the stress spawn counts AND
    // enables the engine's tick budget + skip policy.
    game.worldState().stressMode        = stressMode;

    threadmaxx_vk::VulkanRenderer::Config vrcfg;
    vrcfg.width  = kWidth;
    vrcfg.height = kHeight;
    vrcfg.framesInFlight = 2;
    vrcfg.enableValidation = std::getenv("THREADMAXX_VK_VALIDATE") != nullptr;
    auto renderer = std::make_unique<threadmaxx_vk::VulkanRenderer>(&engine, window, vrcfg);
    engine.setRenderer(renderer.get());

    WindowUserData ud{&engine, &game.worldState()};
    glfwSetWindowUserPointer(window, &ud);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    rpg::installInputCallbacks(window);

    if (!engine.initialize(game)) {
        std::fprintf(stderr, "[rpg_demo] engine.initialize failed\n");
        return 1;
    }

    std::printf("[rpg_demo] running %s — Esc / window close to exit\n",
                maxTicks ? "bounded" : "unbounded");

    constexpr double kFixedDt = 1.0 / 60.0;
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    std::uint64_t tick = 0;
    while (!glfwWindowShouldClose(window) && !engine.quitRequested()) {
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
        rpg::pollContinuousInput(window, kFixedDt);

        engine.step();
        ++tick;

        if (maxTicks && tick >= maxTicks) {
            std::printf("[rpg_demo] reached %llu ticks; exiting\n",
                        static_cast<unsigned long long>(maxTicks));
            break;
        }

        // Pace to ~60 Hz so a vsync-disabled swapchain doesn't burn CPU.
        const auto now = clock::now();
        const double elapsedSecs =
            std::chrono::duration_cast<std::chrono::duration<double>>(now - last).count();
        if (elapsedSecs < kFixedDt) {
            const auto sleepNs = std::chrono::nanoseconds(
                static_cast<long long>((kFixedDt - elapsedSecs) * 1e9));
            std::this_thread::sleep_for(sleepNs);
        }
        last = clock::now();
    }

    engine.shutdown();

    std::printf("[rpg_demo] ran %llu ticks; %llu frames submitted\n",
                static_cast<unsigned long long>(tick),
                static_cast<unsigned long long>(renderer->framesSubmitted()));

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
