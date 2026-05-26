// tou2d — Tunnels of the Underworld 2D adaptation, Milestone 1.
//
// Opens a GLFW window, brings up the threadmaxx engine + Vulkan
// renderer with an orthographic camera, and runs the simulation
// until Ctrl-C / window close. Acceptance check for M1: thrust /
// turn the ship with arrow keys, watch it fall under gravity.

#include "CameraSystem.hpp"
#include "TouGame.hpp"

#include <threadmaxx_vk/VulkanRenderer.hpp>

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/World.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {

constexpr std::uint32_t kInitialWidth  = 1280;
constexpr std::uint32_t kInitialHeight = 720;

/// 8-vertex / 36-index unit cube centered at the origin. Used as the
/// renderer's default mesh — every ship instance draws this cube
/// scaled by its Transform. M2 swaps in real 2D sprite geometry.
///
/// Vertex layout = pos[3] + normal[3] (24-byte stride, matches
/// `opaque.vert`'s binding 0).
const std::vector<float>& cubeVertices() {
    static const std::vector<float> v = {
        // -X face
        -0.5f, -0.5f, -0.5f,  -1.0f, 0.0f, 0.0f,
        -0.5f, -0.5f,  0.5f,  -1.0f, 0.0f, 0.0f,
        -0.5f,  0.5f,  0.5f,  -1.0f, 0.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,  -1.0f, 0.0f, 0.0f,
        // +X face
         0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,
         0.5f,  0.5f, -0.5f,   1.0f, 0.0f, 0.0f,
         0.5f,  0.5f,  0.5f,   1.0f, 0.0f, 0.0f,
         0.5f, -0.5f,  0.5f,   1.0f, 0.0f, 0.0f,
        // -Y face
        -0.5f, -0.5f, -0.5f,   0.0f,-1.0f, 0.0f,
         0.5f, -0.5f, -0.5f,   0.0f,-1.0f, 0.0f,
         0.5f, -0.5f,  0.5f,   0.0f,-1.0f, 0.0f,
        -0.5f, -0.5f,  0.5f,   0.0f,-1.0f, 0.0f,
        // +Y face
        -0.5f,  0.5f, -0.5f,   0.0f, 1.0f, 0.0f,
        -0.5f,  0.5f,  0.5f,   0.0f, 1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,   0.0f, 1.0f, 0.0f,
         0.5f,  0.5f, -0.5f,   0.0f, 1.0f, 0.0f,
        // -Z face
        -0.5f, -0.5f, -0.5f,   0.0f, 0.0f,-1.0f,
        -0.5f,  0.5f, -0.5f,   0.0f, 0.0f,-1.0f,
         0.5f,  0.5f, -0.5f,   0.0f, 0.0f,-1.0f,
         0.5f, -0.5f, -0.5f,   0.0f, 0.0f,-1.0f,
        // +Z face (player-facing — winding is CCW from camera so back-
        // face culling keeps it visible at z = +50 looking down -Z).
        -0.5f, -0.5f,  0.5f,   0.0f, 0.0f, 1.0f,
         0.5f, -0.5f,  0.5f,   0.0f, 0.0f, 1.0f,
         0.5f,  0.5f,  0.5f,   0.0f, 0.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,   0.0f, 0.0f, 1.0f,
    };
    return v;
}

const std::vector<std::uint16_t>& cubeIndices() {
    static const std::vector<std::uint16_t> i = {
         0,  1,  2,   0,  2,  3,    // -X
         4,  5,  6,   4,  6,  7,    // +X
         8,  9, 10,   8, 10, 11,    // -Y
        12, 13, 14,  12, 14, 15,    // +Y
        16, 17, 18,  16, 18, 19,    // -Z
        20, 21, 22,  20, 22, 23,    // +Z
    };
    return i;
}

struct AppCtx {
    threadmaxx::Engine* engine = nullptr;
    tou2d::TouGame*     game   = nullptr;
};

void glfwResizeCb(GLFWwindow* win, int width, int height) {
    auto* ctx = static_cast<AppCtx*>(glfwGetWindowUserPointer(win));
    if (!ctx) return;
    if (width <= 0 || height <= 0) return;
    if (ctx->engine) {
        ctx->engine->notifyResize(static_cast<std::uint32_t>(width),
                                  static_cast<std::uint32_t>(height));
    }
    if (ctx->game && ctx->game->cameraSystem()) {
        ctx->game->cameraSystem()->setViewport(
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height));
    }
}

} // namespace

int main(int argc, char** argv) {
    std::uint64_t maxTicks = 0;
    std::string   levelDir;

    // Lightweight arg parse — supports any order of:
    //   <N>            : bounded run for N ticks (otherwise headless / Ctrl-C)
    //   --level <path> : load imported level dir produced by tou2d_import_lev
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--level" && i + 1 < argc) {
            levelDir = argv[++i];
        } else if (!a.empty() && std::isdigit(static_cast<unsigned char>(a[0]))) {
            maxTicks = std::strtoull(a.c_str(), nullptr, 10);
        } else {
            std::fprintf(stderr, "[tou2d] unknown arg '%s'\n", a.c_str());
            return 2;
        }
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "[tou2d] glfwInit failed\n");
        return 1;
    }
    if (!glfwVulkanSupported()) {
        std::fprintf(stderr, "[tou2d] Vulkan loader not available\n");
        glfwTerminate();
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(static_cast<int>(kInitialWidth),
                                          static_cast<int>(kInitialHeight),
                                          "threadmaxx — tou2d (M1)",
                                          nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[tou2d] glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }

    threadmaxx::Config cfg;
    threadmaxx::Engine engine(cfg);

    tou2d::TouGame game(window);
    if (!levelDir.empty()) {
        game.setLevelDir(levelDir);
        std::printf("[tou2d] loading level from %s\n", levelDir.c_str());
    }

    threadmaxx_vk::VulkanRenderer::Config vrcfg;
    vrcfg.width          = kInitialWidth;
    vrcfg.height         = kInitialHeight;
    vrcfg.framesInFlight = 2;
    vrcfg.enableValidation = std::getenv("THREADMAXX_VK_VALIDATE") != nullptr;
    auto renderer = std::make_unique<threadmaxx_vk::VulkanRenderer>(
        &engine, window, vrcfg);

    // Install the renderer BEFORE engine.initialize so the engine's
    // initialize() runs renderer->initialize() too.
    engine.setRenderer(renderer.get());

    AppCtx ctx;
    ctx.engine = &engine;
    ctx.game   = &game;
    glfwSetWindowUserPointer(window, &ctx);
    glfwSetFramebufferSizeCallback(window, glfwResizeCb);

    if (!engine.initialize(game)) {
        std::fprintf(stderr, "[tou2d] engine.initialize failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Upload the default cube mesh AFTER engine.initialize — at this
    // point renderer->initialize() has run and `setDefaultMeshFromData`
    // can hit the loader's upload path. Failure here just leaves the
    // renderer's built-in procedural cube in place — ship still
    // renders (mirrors the rpg_demo fallback posture).
    {
        const bool installed = renderer->setDefaultMeshFromData(
            cubeVertices(), cubeIndices());
        std::printf("[tou2d] default cube mesh installed=%d\n", int(installed));
    }

    // Seed the camera viewport — onSetup ran without our resize hook
    // having ever fired, so it still holds the constructor default.
    if (game.cameraSystem()) {
        game.cameraSystem()->setViewport(kInitialWidth, kInitialHeight);
    }

    std::printf("[tou2d] running %s — Ctrl-C / window close to exit\n",
                maxTicks ? "bounded" : "unbounded");
    std::printf("[tou2d] controls: arrow keys to thrust/turn, gravity is on\n");

    std::uint64_t tick = 0;
    while (!glfwWindowShouldClose(window) && !engine.quitRequested()) {
        glfwPollEvents();
        engine.step();
        ++tick;
        if (maxTicks && tick >= maxTicks) {
            std::printf("[tou2d] reached %llu ticks; exiting\n",
                        static_cast<unsigned long long>(maxTicks));
            break;
        }
    }

    // Headless verification: log the ship's final position so a bounded
    // smoke run (`threadmaxx_tou2d 600`) can be checked from CI / stdout.
    // Removed once a proper acceptance harness lands.
    if (const auto* finalT = engine.world().tryGetTransform(game.playerShip())) {
        std::printf("[tou2d] final ship pos = (%.2f, %.2f, %.2f); tick=%llu\n",
                    finalT->position.x, finalT->position.y, finalT->position.z,
                    static_cast<unsigned long long>(tick));
    }

    engine.shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
