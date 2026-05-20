// 3D RPG demo — §3.2 batch 10 main entry. Wires GLFW + Vulkan
// renderer + threadmaxx engine + the DemoGame.
//
// Controls:
//   W / S / Up / Down     forward / back
//   A / D / Left / Right  strafe left / right
//   Q / E                 rotate camera yaw
//   Scroll wheel          zoom in / out
//   F                     sword swing (combat)
//   V                     toggle aim PIP camera
//   F1                    toggle Chrome-trace capture (/tmp/rpg_demo_trace.*.json)
//   F5                    synchronous quick-save
//   F8                    asynchronous quick-save
//   F9                    load and restore from the saved file
//   F12                   reload shaders (emits AssetReloaded)
//   Esc / window-close    exit cleanly
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
#include "ObjLoader.hpp"
#include "SkinnedCapsule.hpp"

#include <cmath>

#include <threadmaxx_vk/VulkanRenderer.hpp>

#include <threadmaxx/Config.hpp>
#include <threadmaxx/Engine.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
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
    // workerCount = 0 → JobSystem picks max(1, hardware_concurrency - 1).
    // The brain + cube-render + culling systems all scale with worker
    // count under `--stress`; pinning to 8 left a 72-core box at 7%
    // CPU. Auto-sizing fills out the pool.
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
    // 2026-05-20 — framesInFlight=3 lets the GPU pipeline overlap
    // two earlier frames while the sim builds the next; under
    // --stress this drops the per-tick `vkWaitSemaphores` wait
    // because the slot's previous GPU work is usually done by the
    // time we come back around. 2 was the historical default.
    vrcfg.framesInFlight = 3;
    vrcfg.enableValidation = std::getenv("THREADMAXX_VK_VALIDATE") != nullptr;
    auto renderer = std::make_unique<threadmaxx_vk::VulkanRenderer>(&engine, window, vrcfg);
    engine.setRenderer(renderer.get());

    // §3.11 batch 9b.2b — wire the multi-mesh registration callback
    // before `engine.initialize`. By the time `onSetup` fires the
    // renderer's `initialize()` has run, so the loader is ready and
    // `registerMeshFromData` returns a real meshId. The callback is
    // null-safe — headless tests skip this step and pickups fall
    // back to the default cube.
    game.setRegisterMeshFn(
        [r = renderer.get()](std::span<const float>         vertices,
                             std::span<const std::uint16_t> indices) {
            return r->registerMeshFromData(vertices, indices);
        });

    // §3.11 batch 9b.3 — wire the F12 shader-reload callback. The
    // callback fires from `HudSystem::preStep` when the user hits
    // F12; the renderer's `reloadShaders` walks each tracked pipeline
    // shader, calls `engine.markResourceStale<Shader>(id)`, and lets
    // the engine's loader → AssetReloaded → renderer-subscriber chain
    // rebuild the affected pipelines. Headless tests leave this null
    // and F12 becomes a no-op.
    game.setReloadShadersFn([r = renderer.get()] { r->reloadShaders(); });

    WindowUserData ud{&engine, &game.worldState()};
    glfwSetWindowUserPointer(window, &ud);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    rpg::installInputCallbacks(window);

    if (!engine.initialize(game)) {
        std::fprintf(stderr, "[rpg_demo] engine.initialize failed\n");
        return 1;
    }

    // §3.11 batch 9b.2 — replace the procedural unit-cube the renderer
    // built during initialize() with one loaded from an .obj asset.
    // Exercises the full demo-side `parseObjFile` → renderer-side
    // `createMesh` + `setDefaultMesh` upload path end-to-end. Failure
    // silently leaves the procedural cube in place — the demo is still
    // playable, the log line just calls out which path is live.
    {
        const std::string objPath = std::string(RPG_DEMO_SOURCE_DIR) +
                                    "/assets/cube.obj";
        const auto parsed = rpg::parseObjFile(objPath);
        if (parsed.ok) {
            const bool installed = renderer->setDefaultMeshFromData(
                std::span<const float>(parsed.mesh.vertices),
                std::span<const std::uint16_t>(parsed.mesh.indices));
            std::printf("[rpg_demo] obj asset: %s corners=%u installed=%d\n",
                        objPath.c_str(), parsed.mesh.cornerCount, int(installed));
        } else {
            std::printf("[rpg_demo] obj asset: %s — fallback to procedural cube (%s)\n",
                        objPath.c_str(), parsed.error.c_str());
        }
    }

    // §3.11.7b.5 batch 9b.4.c — register the procedural skinned
    // capsule mesh, stash the id in WorldState so
    // `SkinnedRenderSystem` will emit DrawItems for it. Failure
    // (returns -1) leaves `skinnedMeshId == 0` and the system falls
    // silent — vanilla unskinned demo still works.
    {
        const auto cap = rpg::makeSkinnedCapsule();
        const std::int32_t skinId = renderer->registerSkinnedMeshFromData(
            std::span<const float>(cap.vertices),
            std::span<const std::uint16_t>(cap.indices));
        if (skinId > 0) {
            game.worldState().skinnedMeshId = skinId;
            std::printf("[rpg_demo] skinned capsule: meshId=%d verts=%zu tris=%zu\n",
                        skinId, cap.vertices.size() / 14,
                        cap.indices.size() / 3);
        } else {
            std::printf("[rpg_demo] skinned capsule: registration failed; "
                        "skinning path disabled\n");
        }
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

        // §3.11.7b.5 batch 9b.4.c — push the per-frame bone matrices
        // for the procedural 2-bone capsule. Bone 0 (root) is the
        // identity; bone 1 (tip) rotates around the Z axis with a
        // sine wave so the upper half of the capsule visibly sways.
        // Matrices are column-major (Vulkan std140 convention).
        if (game.worldState().skinnedMeshId > 0) {
            const float t = static_cast<float>(tick) * static_cast<float>(kFixedDt);
            const float angle = 0.4f * std::sin(2.5f * t);   // ±0.4 rad ≈ ±23°
            const float c = std::cos(angle), s = std::sin(angle);
            // Column-major 4×4 — stored row-by-row in array of 16:
            //   [0..3]   col 0
            //   [4..7]   col 1
            //   [8..11]  col 2
            //   [12..15] col 3
            float bones[32] = {
                // Bone 0: identity.
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1,
                // Bone 1: rotation by `angle` around the Z axis.
                // Because vertex y at y=2 is on the rotation center
                // (we want pure rotation around the capsule's own
                // y axis at the bone joint), the rotation acts in
                // the X plane.
                 c, s, 0, 0,
                -s, c, 0, 0,
                 0, 0, 1, 0,
                 0, 0, 0, 1,
            };
            renderer->setBoneMatrices(std::span<const float>(bones, 32));
        }

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
