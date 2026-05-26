// tou2d — Tunnels of the Underworld 2D adaptation, Milestone 1.
//
// Opens a GLFW window, brings up the threadmaxx engine + Vulkan
// renderer with an orthographic camera, and runs the simulation
// until Ctrl-C / window close. Acceptance check for M1: thrust /
// turn the ship with arrow keys, watch it fall under gravity.

#include "CameraSystem.hpp"
#include "DemoTypes.hpp"
#include "TouGame.hpp"

#include <threadmaxx_vk/VulkanRenderer.hpp>

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/World.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb/stb_image.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
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

    // M3.2 — install the imported level's visual.jpg as a WORLD-SPACE
    // background. The image is positioned to span the level extent
    // (cellsX*tile × cellsY*tile in world units), so the camera sees
    // the same image position as it would see a same-world-position
    // entity. We keep the decoded RGBA on the CPU so the
    // BulletTerrainSystem destroy callback can paint destroyed tiles
    // black and re-upload; the JPG IS the destructible terrain.
    //
    // Painter is on the stack and outlives the engine loop below — by
    // the time the loop ends the engine has shut down and the callback
    // is no longer reachable.
    struct BackgroundPainter {
        std::vector<std::uint8_t>          pixels;
        int                                width    = 0;
        int                                height   = 0;
        int                                halfX    = 0;
        int                                halfY    = 0;
        threadmaxx_vk::VulkanRenderer*     renderer = nullptr;

        // Per-frame dirty bbox in image pixels. Coalesced inside the
        // tick; flushed once after `engine.step()` returns so the
        // expensive Vulkan upload + barrier round-trip happens at most
        // once per frame regardless of how many tiles were destroyed.
        // `dirty` flips true when any tile was painted; `flush()`
        // resets it. Scratch row-buffer reused to ship the bbox-cut
        // sub-rect contiguously to `updateBackgroundRegion`.
        bool                      dirty   = false;
        int                       minX = 0, minY = 0;
        int                       maxX = 0, maxY = 0;
        std::vector<std::uint8_t> uploadScratch;

        void paintTile(int worldCellX, int worldCellY) {
            // Inverse of LevelLoader's image -> world mapping:
            //   worldCellX = imageCx - halfX  =>  imageCx = worldCellX + halfX
            //   worldCellY = halfY - imageCy  =>  imageCy = halfY - worldCellY
            const int imgCx = worldCellX + halfX;
            const int imgCy = halfY     - worldCellY;
            const int tilePx = tou2d::kImportedPxPerTile;
            const int px0 = imgCx * tilePx;
            const int py0 = imgCy * tilePx;

            const int clampedX0 = std::max(0, px0);
            const int clampedY0 = std::max(0, py0);
            const int clampedX1 = std::min(width,  px0 + tilePx);
            const int clampedY1 = std::min(height, py0 + tilePx);
            if (clampedX1 <= clampedX0 || clampedY1 <= clampedY0) return;

            // M3.4 — anti-chunkiness paint. Solid black fill makes the
            // pxPerTile grid visible after destruction; instead darken
            // each pixel by an envelope that:
            //   * fades from ~75% of original at the rect's edge to ~0%
            //     at the rect's interior (chebyshev distance from edge)
            //   * adds a per-pixel hash-jitter (±10%) so the dark zone
            //     reads as rocky texture rather than a flat fill
            // When two adjacent tiles fall, each keeps its own edge
            // falloff and the seam reads as a damage-crack instead of a
            // straight grid line.
            const int   rectW = clampedX1 - clampedX0;
            const int   rectH = clampedY1 - clampedY0;
            const float half  = static_cast<float>(std::min(rectW, rectH)) * 0.5f;
            const float invHalf = half > 0.0f ? 1.0f / half : 1.0f;
            for (int y = clampedY0; y < clampedY1; ++y) {
                const int dyEdge = std::min(y - clampedY0, clampedY1 - 1 - y);
                for (int x = clampedX0; x < clampedX1; ++x) {
                    const int dxEdge = std::min(x - clampedX0, clampedX1 - 1 - x);
                    const int edge   = std::min(dxEdge, dyEdge);
                    const float t    = std::min(1.0f, static_cast<float>(edge) * invHalf);

                    // Cheap xor-mix hash on (x, y). Top byte gives a
                    // uniform [-1, +1] when normalized; scale to ±0.1.
                    const std::uint32_t h =
                        (static_cast<std::uint32_t>(x) * 0x9E3779B1u) ^
                        (static_cast<std::uint32_t>(y) * 0x85EBCA77u);
                    const float jitter =
                        (static_cast<float>(h >> 24) / 255.0f - 0.5f) * 0.2f;

                    // 0.25 at edge → 1.10 at center (clamped to 1.0).
                    const float darken =
                        std::clamp(0.25f + 0.85f * t + jitter, 0.0f, 1.0f);
                    const float keep   = 1.0f - darken;

                    const std::size_t off =
                        (static_cast<std::size_t>(y) *
                         static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) * 4u;
                    pixels[off + 0] = static_cast<std::uint8_t>(
                        static_cast<float>(pixels[off + 0]) * keep);
                    pixels[off + 1] = static_cast<std::uint8_t>(
                        static_cast<float>(pixels[off + 1]) * keep);
                    pixels[off + 2] = static_cast<std::uint8_t>(
                        static_cast<float>(pixels[off + 2]) * keep);
                    pixels[off + 3] = 255;
                }
            }

            if (!dirty) {
                dirty = true;
                minX = clampedX0; minY = clampedY0;
                maxX = clampedX1; maxY = clampedY1;
            } else {
                minX = std::min(minX, clampedX0);
                minY = std::min(minY, clampedY0);
                maxX = std::max(maxX, clampedX1);
                maxY = std::max(maxY, clampedY1);
            }
        }

        void flush() {
            if (!dirty || !renderer) return;
            const int rectW = maxX - minX;
            const int rectH = maxY - minY;
            const std::size_t bytes =
                static_cast<std::size_t>(rectW) *
                static_cast<std::size_t>(rectH) * 4u;
            uploadScratch.resize(bytes);
            for (int row = 0; row < rectH; ++row) {
                const std::uint8_t* src = pixels.data() +
                    (static_cast<std::size_t>(minY + row) *
                     static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(minX)) * 4u;
                std::uint8_t* dst = uploadScratch.data() +
                    static_cast<std::size_t>(row) *
                    static_cast<std::size_t>(rectW) * 4u;
                std::memcpy(dst, src,
                            static_cast<std::size_t>(rectW) * 4u);
            }
            renderer->updateBackgroundRegion(
                static_cast<std::uint32_t>(minX),
                static_cast<std::uint32_t>(minY),
                static_cast<std::uint32_t>(rectW),
                static_cast<std::uint32_t>(rectH),
                std::span<const std::uint8_t>(uploadScratch.data(), bytes));
            dirty = false;
        }
    } bgPainter;

    if (!levelDir.empty()) {
        const std::filesystem::path bgPath =
            std::filesystem::path(levelDir) / "visual.jpg";
        int w = 0, h = 0, n = 0;
        if (stbi_uc* pixels = stbi_load(bgPath.string().c_str(),
                                        &w, &h, &n, /*req_comp=*/4)) {
            // Pad the image up to (cellsX * pxPerTile) × (cellsY *
            // pxPerTile) so the texture's pixel grid is EXACT-aligned
            // with the cell grid. Without this step the JPG's
            // image-pixel-to-world-pixel ratio drifts cumulatively
            // across the image (~21 wu by the right edge on the
            // jungle level), which makes the visible image misalign
            // with the collision grid (and the destruction paint
            // lands one tile right of where the user shot). The pad
            // is opaque black (0,0,0,255) — matches "void / off-level"
            // intent.
            const int cellsX  = game.levelCellsX() > 0 ? game.levelCellsX() : 1;
            const int cellsY  = game.levelCellsY() > 0 ? game.levelCellsY() : 1;
            const int tilePx  = tou2d::kImportedPxPerTile;
            const int paddedW = cellsX * tilePx;
            const int paddedH = cellsY * tilePx;
            const std::size_t paddedBytes =
                static_cast<std::size_t>(paddedW) *
                static_cast<std::size_t>(paddedH) * 4u;
            bgPainter.pixels.assign(paddedBytes, std::uint8_t{0});
            for (std::size_t i = 3; i < paddedBytes; i += 4) {
                bgPainter.pixels[i] = 0xFF;  // opaque
            }
            const int copyH = std::min(h, paddedH);
            const int copyW = std::min(w, paddedW);
            for (int y = 0; y < copyH; ++y) {
                const std::uint8_t* srcRow =
                    pixels + static_cast<std::ptrdiff_t>(y) * w * 4;
                std::uint8_t* dstRow =
                    bgPainter.pixels.data() +
                    static_cast<std::ptrdiff_t>(y) * paddedW * 4;
                std::memcpy(dstRow, srcRow,
                            static_cast<std::size_t>(copyW) * 4u);
            }
            bgPainter.width    = paddedW;
            bgPainter.height   = paddedH;
            bgPainter.halfX    = cellsX / 2;
            bgPainter.halfY    = cellsY / 2;
            bgPainter.renderer = renderer.get();

            const bool ok = renderer->setBackgroundFromRgba(
                std::span<const std::uint8_t>(bgPainter.pixels.data(),
                                              bgPainter.pixels.size()),
                static_cast<std::uint32_t>(paddedW),
                static_cast<std::uint32_t>(paddedH));

            // World-space placement of the JPG quad — matches the cell
            // grid's actual extent. For LevelLoader's mapping
            //   worldCellX = imgCx - halfX,  worldCellY = halfY - imgCy
            // (halfX/Y = cellsX/Y / 2, integer division) the cells run
            //   X: [-halfX, cellsX - halfX - 1]
            //   Y: [-(cellsY - halfY - 1), halfY]
            // For even cellsX, X is asymmetric → center at -tile/2; for
            // even cellsY, Y is asymmetric → center at +tile/2.
            const float tile  = tou2d::kTileWorldUnits;
            const int   halfX = cellsX / 2;
            const int   halfY = cellsY / 2;
            const float minWorldX = -static_cast<float>(halfX) * tile - tile * 0.5f;
            const float maxWorldX = static_cast<float>(cellsX - halfX - 1) * tile + tile * 0.5f;
            const float minWorldY = -static_cast<float>(cellsY - halfY - 1) * tile - tile * 0.5f;
            const float maxWorldY = static_cast<float>(halfY) * tile + tile * 0.5f;
            renderer->setBackgroundWorldExtent(
                (maxWorldX - minWorldX) * 0.5f,
                (maxWorldY - minWorldY) * 0.5f,
                (maxWorldX + minWorldX) * 0.5f,
                (maxWorldY + minWorldY) * 0.5f);

            game.setTileDestroyCallback(
                [&bgPainter](int cx, int cy) { bgPainter.paintTile(cx, cy); });

            stbi_image_free(pixels);
            std::printf("[tou2d] background %s: %dx%d -> padded %dx%d "
                        "(channels=%d) installed=%d\n",
                        bgPath.string().c_str(), w, h, paddedW, paddedH,
                        n, int(ok));
        } else {
            std::fprintf(stderr,
                "[tou2d] background %s: stbi_load failed (%s)\n",
                bgPath.string().c_str(), stbi_failure_reason());
        }
    }

    std::printf("[tou2d] running %s — Ctrl-C / window close to exit\n",
                maxTicks ? "bounded" : "unbounded");
    std::printf("[tou2d] controls: arrow keys to thrust/turn, gravity is on\n");

    // Frame pacing — pin the loop to 60 Hz. Engine integrates in fixed
    // 1/60 s steps internally, so wall-clock pacing here is what
    // determines visible speed. Bounded runs (`maxTicks` set, used by
    // smoke tests) skip the sleep so they finish promptly.
    using clock = std::chrono::steady_clock;
    constexpr auto kFrameInterval = std::chrono::nanoseconds(16'666'667);  // 1/60 s
    auto nextFrame = clock::now() + kFrameInterval;

    std::uint64_t tick = 0;
    while (!glfwWindowShouldClose(window) && !engine.quitRequested()) {
        glfwPollEvents();
        engine.step();
        // Flush any per-tick background dirty rect once after the
        // tick's commits — the painter accumulated paint events from
        // BulletTerrain + Collision destroy callbacks; one
        // `updateBackgroundRegion` covers the union bbox.
        bgPainter.flush();
        ++tick;
        if (maxTicks && tick >= maxTicks) {
            std::printf("[tou2d] reached %llu ticks; exiting\n",
                        static_cast<unsigned long long>(maxTicks));
            break;
        }
        if (!maxTicks) {
            std::this_thread::sleep_until(nextFrame);
            nextFrame += kFrameInterval;
            // If we fell behind by more than one frame (e.g. window
            // dragged), resync rather than playing catch-up.
            const auto now = clock::now();
            if (nextFrame < now) nextFrame = now + kFrameInterval;
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
