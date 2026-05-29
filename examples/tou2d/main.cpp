// tou2d — Tunnels of the Underworld 2D adaptation, Milestone 1.
//
// Opens a GLFW window, brings up the threadmaxx engine + Vulkan
// renderer with an orthographic camera, and runs the simulation
// until Ctrl-C / window close. Acceptance check for M1: thrust /
// turn the ship with arrow keys, watch it fall under gravity.

#include "CameraSystem.hpp"
#include "DemoTypes.hpp"
#include "InputSystem.hpp"
#include "ProceduralLevel.hpp"
#include "Replay.hpp"
#include "SpriteCompositor.hpp"
#include "TouGame.hpp"
#include "UISystem.hpp"
#include "ui/Font.hpp"
#include "ui/UiOverlayBitmap.hpp"

#include <threadmaxx_vk/VulkanRenderer.hpp>

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/World.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb/stb_image.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
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
    std::uint64_t      maxTicks  = 0;
    std::string        levelDir;
    tou2d::MatchMode   matchMode = tou2d::MatchMode::Deathmatch;
    // M5.1 — humans + bots configurable. Defaults match the prior
    // 1 human / 3 bots posture so smoke tests keep their shape.
    std::uint8_t       numHumans = 1;
    std::uint8_t       numBots   = 3;
    // M5.4 — replay capture + playback. At most one of these may be set.
    std::string        recordPath;
    std::string        playPath;
    // M5.5 — procedural level generator. When `useGen` is true the
    // generator runs in `TouGame::onSetup` instead of loading levelDir.
    // `--gen` enables the generator with default config; `--gen=<seed>`
    // or `--gen-seed=N` overrides the seed; --gen-level / --gen-density
    // / --gen-perim tune the canvas. Mutex with --level.
    bool               useGen     = false;
    tou2d::ProceduralLevelConfig genCfg{};
    // M5.6 — default special-weapon kind for every spawned ship.
    // Selects an entry in `kSpecialWeaponSpecs`. `--special=<name>`
    // chooses by token; default `spread` keeps pre-M5.6 behaviour.
    std::uint8_t       specialKind = static_cast<std::uint8_t>(tou2d::SpecialKind::Spread);
    // M5.7 — repair-tile sprinkle count for the procedural generator.
    // 0 = pre-M5.7 behaviour (no Repair cells). The default of 8 is
    // applied below if --gen is on AND the user didn't pass --repair-tiles.
    bool               repairTilesSpecified = false;
    std::uint8_t       repairTiles          = 0;

    // Lightweight arg parse — supports any order of:
    //   <N>                  : bounded run for N ticks
    //   --level <path>       : load imported level dir
    //   --mode=<dm|lss>      : deathmatch (default) or last-ship-standing
    //   --humans=N           : 1..4
    //   --bots=N             : 0..63
    //   --record <path>      : capture inputs + commitHash to <path>
    //   --play <path>        : replay from <path>
    //   --gen[=seed]         : enable procedural generator (M5.5)
    //   --gen-seed=N         : explicit RNG seed (default 0)
    //   --gen-level=N        : 0..4 size class (default 2)
    //   --gen-density=N      : 0..100 stuff density (default 50)
    //   --gen-perim=<0|1>    : 1 cell of bedrock around the canvas (default 1)
    //   --special=<token>    : starting special weapon. Tokens (M5.6+M5.7+M5.8):
    //                          spread (default), rapid, sniper, quintet,
    //                          heavy, quad, shotgun, mine, bouncer, homer
    //   --repair-tiles=N     : sprinkle N Repair pickup tiles into the
    //                          procedural level (M5.7). 0..255, default
    //                          8 when --gen is active, 0 otherwise.
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--level" && i + 1 < argc) {
            levelDir = argv[++i];
        } else if (a == "--record" && i + 1 < argc) {
            recordPath = argv[++i];
        } else if (a == "--play" && i + 1 < argc) {
            playPath = argv[++i];
        } else if (a == "--gen") {
            useGen = true;
        } else if (a.rfind("--gen=", 0) == 0) {
            useGen = true;
            const unsigned long v = std::strtoul(a.c_str() + 6, nullptr, 0);
            genCfg.seed = static_cast<std::uint32_t>(v);
        } else if (a.rfind("--gen-seed=", 0) == 0) {
            useGen = true;
            const unsigned long v = std::strtoul(a.c_str() + 11, nullptr, 0);
            genCfg.seed = static_cast<std::uint32_t>(v);
        } else if (a.rfind("--gen-level=", 0) == 0) {
            useGen = true;
            const long v = std::strtol(a.c_str() + 12, nullptr, 10);
            if (v < 0 || v > 4) {
                std::fprintf(stderr,
                    "[tou2d] --gen-level=%ld — expected 0..4\n", v);
                return 2;
            }
            genCfg.ggLevel = static_cast<std::uint8_t>(v);
        } else if (a.rfind("--gen-density=", 0) == 0) {
            useGen = true;
            const long v = std::strtol(a.c_str() + 14, nullptr, 10);
            if (v < 0 || v > 100) {
                std::fprintf(stderr,
                    "[tou2d] --gen-density=%ld — expected 0..100\n", v);
                return 2;
            }
            genCfg.stuffDensity = static_cast<std::uint8_t>(v);
        } else if (a.rfind("--gen-perim=", 0) == 0) {
            useGen = true;
            const long v = std::strtol(a.c_str() + 12, nullptr, 10);
            if (v < 0 || v > 1) {
                std::fprintf(stderr,
                    "[tou2d] --gen-perim=%ld — expected 0 or 1\n", v);
                return 2;
            }
            genCfg.perimeterBedrock = static_cast<std::uint8_t>(v);
        } else if (a.rfind("--special=", 0) == 0) {
            const std::string val = a.substr(10);
            if      (val == "spread")  specialKind = static_cast<std::uint8_t>(tou2d::SpecialKind::Spread);
            else if (val == "rapid")   specialKind = static_cast<std::uint8_t>(tou2d::SpecialKind::Rapid);
            else if (val == "sniper")  specialKind = static_cast<std::uint8_t>(tou2d::SpecialKind::Sniper);
            else if (val == "quintet") specialKind = static_cast<std::uint8_t>(tou2d::SpecialKind::Quintet);
            else if (val == "heavy")   specialKind = static_cast<std::uint8_t>(tou2d::SpecialKind::Heavy);
            else if (val == "quad")    specialKind = static_cast<std::uint8_t>(tou2d::SpecialKind::Quad);
            else if (val == "shotgun") specialKind = static_cast<std::uint8_t>(tou2d::SpecialKind::Shotgun);
            else if (val == "mine")    specialKind = static_cast<std::uint8_t>(tou2d::SpecialKind::Mine);
            else if (val == "bouncer") specialKind = static_cast<std::uint8_t>(tou2d::SpecialKind::Bouncer);
            else if (val == "homer")   specialKind = static_cast<std::uint8_t>(tou2d::SpecialKind::Homer);
            else {
                std::fprintf(stderr,
                    "[tou2d] --special=%s — expected spread|rapid|sniper|quintet|heavy|quad|shotgun|mine|bouncer|homer\n",
                    val.c_str());
                return 2;
            }
        } else if (a.rfind("--repair-tiles=", 0) == 0) {
            const long v = std::strtol(a.c_str() + 15, nullptr, 10);
            if (v < 0 || v > 255) {
                std::fprintf(stderr,
                    "[tou2d] --repair-tiles=%ld — expected 0..255\n", v);
                return 2;
            }
            repairTiles          = static_cast<std::uint8_t>(v);
            repairTilesSpecified = true;
        } else if (a.rfind("--mode=", 0) == 0) {
            const std::string val = a.substr(7);
            if (val == "dm" || val == "deathmatch") {
                matchMode = tou2d::MatchMode::Deathmatch;
            } else if (val == "lss" || val == "lastshipstanding") {
                matchMode = tou2d::MatchMode::LastShipStanding;
            } else {
                std::fprintf(stderr,
                    "[tou2d] --mode=%s — expected dm|lss\n", val.c_str());
                return 2;
            }
        } else if (a.rfind("--humans=", 0) == 0) {
            const long v = std::strtol(a.c_str() + 9, nullptr, 10);
            if (v < 1 || v > static_cast<long>(tou2d::kMaxHumans)) {
                std::fprintf(stderr,
                    "[tou2d] --humans=%ld — expected 1..%u\n",
                    v, unsigned(tou2d::kMaxHumans));
                return 2;
            }
            numHumans = static_cast<std::uint8_t>(v);
        } else if (a.rfind("--bots=", 0) == 0) {
            const long v = std::strtol(a.c_str() + 7, nullptr, 10);
            if (v < 0 || v > static_cast<long>(tou2d::kMaxBots)) {
                std::fprintf(stderr,
                    "[tou2d] --bots=%ld — expected 0..%u\n",
                    v, unsigned(tou2d::kMaxBots));
                return 2;
            }
            numBots = static_cast<std::uint8_t>(v);
        } else if (!a.empty() && std::isdigit(static_cast<unsigned char>(a[0]))) {
            maxTicks = std::strtoull(a.c_str(), nullptr, 10);
        } else {
            std::fprintf(stderr, "[tou2d] unknown arg '%s'\n", a.c_str());
            return 2;
        }
    }

    // Minimum 2 ships total so deathmatch / LSS has someone to shoot at.
    if (numHumans + numBots < 2) {
        std::fprintf(stderr,
            "[tou2d] need at least 2 entities total (got %u humans + %u bots)\n",
            unsigned(numHumans), unsigned(numBots));
        return 2;
    }

    // M6.1 — "no CLI args" => show MainMenu before gameplay. Any arg
    // (`<N>` bounded tick count, `--humans`, `--bots`, `--level`,
    // `--gen*`, `--special`, `--record`, `--play`, `--mode`,
    // `--repair-tiles`, ...) bypasses the menu: headless smoke,
    // benchmark loops, and replay round-trips are unaffected. The
    // bypass is a UX shortcut — gameplay still defaults to the M5.1
    // shape (1 human + 3 bots, synthetic arena).
    const bool cliMenuBypass = (argc > 1);

    // M5.4 — `--record` and `--play` are mutually exclusive. In play
    // mode the replay file's header overrides --humans / --bots /
    // --mode / --level so the playback ALWAYS matches the recording's
    // setup byte-for-byte.
    if (!recordPath.empty() && !playPath.empty()) {
        std::fprintf(stderr,
            "[tou2d] --record and --play are mutually exclusive\n");
        return 2;
    }
    // M5.5 — `--gen` and `--level` are mutually exclusive. Either the
    // generator picks the canvas or the importer loads one from disk;
    // there is no overlay path.
    if (useGen && !levelDir.empty()) {
        std::fprintf(stderr,
            "[tou2d] --gen and --level are mutually exclusive\n");
        return 2;
    }
    // M5.7 — default repair-tile sprinkle for --gen runs. The CLI
    // default is 8 tiles, applied only when --gen is active and the
    // user didn't explicitly --repair-tiles=N. A --play header trumps
    // this entirely (resolved a few lines below).
    if (useGen) {
        genCfg.repairTileCount =
            repairTilesSpecified ? repairTiles : std::uint8_t{8};
    }
    tou2d::ReplayPlayer replayPlayer;
    if (!playPath.empty()) {
        if (!replayPlayer.open(playPath)) {
            std::fprintf(stderr,
                "[tou2d] --play %s: open failed\n", playPath.c_str());
            return 1;
        }
        numHumans = replayPlayer.numHumans();
        numBots   = replayPlayer.numBots();
        matchMode = replayPlayer.matchMode() == 0
            ? tou2d::MatchMode::Deathmatch
            : tou2d::MatchMode::LastShipStanding;
        // M5.5 — header tells us which level path to use. Gen config in
        // the header trumps any --level / --gen flags on the cli, since
        // playback must match the recording byte-for-byte.
        if (replayPlayer.genConfig().has_value()) {
            useGen = true;
            genCfg = *replayPlayer.genConfig();
            levelDir.clear();
            std::printf("[tou2d] --play %s: header genSeed=0x%08x ggLevel=%u "
                        "stuffD=%u perim=%u repair=%u\n",
                        playPath.c_str(),
                        genCfg.seed,
                        unsigned(genCfg.ggLevel),
                        unsigned(genCfg.stuffDensity),
                        unsigned(genCfg.perimeterBedrock),
                        unsigned(genCfg.repairTileCount));
        } else {
            useGen = false;
            if (!replayPlayer.levelDir().empty()) {
                levelDir = replayPlayer.levelDir();
            }
        }
        std::printf("[tou2d] --play %s: header %u humans + %u bots, mode=%u, level=%s\n",
                    playPath.c_str(),
                    unsigned(numHumans), unsigned(numBots),
                    unsigned(replayPlayer.matchMode()),
                    levelDir.c_str());
        // M5.6 — the recorded specialKind trumps any cli --special.
        specialKind = replayPlayer.specialKind();
        std::printf("[tou2d] --play %s: header specialKind=%u\n",
                    playPath.c_str(), unsigned(specialKind));
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
    if (useGen) {
        game.setGenerationConfig(genCfg);
        std::printf("[tou2d] proc-gen level: seed=0x%08x ggLevel=%u "
                    "stuffD=%u perim=%u repair=%u\n",
                    genCfg.seed,
                    unsigned(genCfg.ggLevel),
                    unsigned(genCfg.stuffDensity),
                    unsigned(genCfg.perimeterBedrock),
                    unsigned(genCfg.repairTileCount));
    } else if (!levelDir.empty()) {
        game.setLevelDir(levelDir);
        std::printf("[tou2d] loading level from %s\n", levelDir.c_str());
    }
    // M4.8 — point the game at the asset root. Defaults to `./assets/`
    // relative to the binary's cwd; the user typically runs from the
    // project root or passes an explicit dir via `--assets`. The
    // SpriteCompositor (declared below) is borrowed for atlas loading.
    {
        const char* envAssets = std::getenv("TOU2D_ASSETS");
        game.setAssetDir(envAssets ? envAssets : "assets");
    }
    tou2d::SpriteCompositor compositor;
    game.setSpriteCompositor(&compositor);
    game.setMatchMode(matchMode);
    game.setPlayerCounts(numHumans, numBots);
    game.setDefaultSpecialKind(static_cast<tou2d::SpecialKind>(specialKind));
    std::printf("[tou2d] match mode: %s\n",
                matchMode == tou2d::MatchMode::LastShipStanding
                    ? "last-ship-standing"
                    : "deathmatch");
    std::printf("[tou2d] players: %u human + %u bots\n",
                unsigned(numHumans), unsigned(numBots));
    {
        // M5.6 / M5.7 — log the resolved special weapon by token.
        const char* tok = "spread";
        switch (static_cast<tou2d::SpecialKind>(specialKind)) {
            case tou2d::SpecialKind::Spread:  tok = "spread";  break;
            case tou2d::SpecialKind::Rapid:   tok = "rapid";   break;
            case tou2d::SpecialKind::Sniper:  tok = "sniper";  break;
            case tou2d::SpecialKind::Quintet: tok = "quintet"; break;
            case tou2d::SpecialKind::Heavy:   tok = "heavy";   break;
            case tou2d::SpecialKind::Quad:    tok = "quad";    break;
            case tou2d::SpecialKind::Shotgun: tok = "shotgun"; break;
            case tou2d::SpecialKind::Mine:    tok = "mine";    break;
            case tou2d::SpecialKind::Bouncer: tok = "bouncer"; break;
            case tou2d::SpecialKind::Homer:   tok = "homer";   break;
        }
        std::printf("[tou2d] special weapon: %s\n", tok);
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

    // M6.0b — bake the bundled UI font at startup. The asset lives at
    // `<assets>/ui/font.ttf` (DejaVu Sans Mono by default — see
    // `assets/ui/README.md` for drop-in instructions). The atlas is
    // baked once at the v1 pixel size and reused for every UI text
    // emit; M6.5's UI-scale slider will additionally cache atlases at
    // 75/100/125/150% tiers. Failure (missing file, bad TTF, oversize
    // codepoint range) leaves `fontAtlas.valid()` false and the UI
    // compositor draws nothing — no crash, no abort.
    tou2d::ui::FontAtlas fontAtlas;
    {
        const std::filesystem::path ttfPath =
            std::filesystem::path(
                std::getenv("TOU2D_ASSETS") ? std::getenv("TOU2D_ASSETS")
                                            : "assets") /
            "ui" / "font.ttf";
        tou2d::ui::FontConfig cfg{};
        cfg.pixelSize = 16;
        fontAtlas = tou2d::ui::bakeFontFromFile(ttfPath.string(), cfg);
        if (fontAtlas.valid()) {
            std::printf("[tou2d] UI font baked from %s: %dx%d atlas, "
                        "%zu glyphs, pixelSize=%d\n",
                        ttfPath.string().c_str(),
                        fontAtlas.atlasW, fontAtlas.atlasH,
                        fontAtlas.glyphs.size(), fontAtlas.pixelSize);
        } else {
            std::fprintf(stderr,
                "[tou2d] UI font bake FAILED (%s) — overlay disabled\n",
                ttfPath.string().c_str());
        }
    }

    // M6.0b — CPU-side RGBA8 framebuffer the per-tick UI emits paint
    // into. Sized to the swapchain extent; resize is rare (only on
    // window resize). One full-frame upload on first tick, partial
    // dirty-bbox uploads thereafter. M6.1 fills it with the active
    // UIScreen's rows + focus highlight; when on UIScreen::None the
    // overlay is cleared (transparent) so gameplay shows through.
    tou2d::ui::UiOverlayBitmap uiOverlay;
    uiOverlay.resize(kInitialWidth, kInitialHeight);
    std::vector<std::uint8_t> uiUploadScratch;
    bool uiOverlayInstalled = false;

    // M6.1 — UI key-edge tracker. We poll GLFW directly each frame
    // (independently of InputSystem, which is paused while a menu is
    // up) and dispatch UiUp/UiDown/UiAccept/UiCancel actions on the
    // RISING edge — held keys do not auto-repeat. The KeyMap defaults
    // (declared once on startup) populate the UI bindings on slot 0.
    const tou2d::KeyMap uiKeyMap = tou2d::makeDefaultKeyMap();
    struct UiKeyEdges {
        bool prev[6] = {};   // index: 0=UiUp 1=UiDown 2=UiLeft 3=UiRight 4=UiAccept 5=UiCancel
        bool rising[6] = {};
        void poll(GLFWwindow* w, const tou2d::KeyMap& km) {
            if (!w) return;
            const auto& row = km.binding[0];
            constexpr std::array<tou2d::Action, 6> kSlots = {
                tou2d::Action::UiUp, tou2d::Action::UiDown,
                tou2d::Action::UiLeft, tou2d::Action::UiRight,
                tou2d::Action::UiAccept, tou2d::Action::UiCancel,
            };
            for (std::size_t i = 0; i < kSlots.size(); ++i) {
                const std::uint16_t key =
                    row[static_cast<std::size_t>(kSlots[i])];
                const bool cur = (key != tou2d::kKeyUnbound) &&
                                 glfwGetKey(w, static_cast<int>(key)) == GLFW_PRESS;
                rising[i] = cur && !prev[i];
                prev[i]   = cur;
            }
        }
    };
    UiKeyEdges uiEdges{};

    // M6.1 — flip into MainMenu when launched without CLI args. The
    // engine starts paused so no ticks accumulate behind the menu;
    // the per-frame UI sync (below) keeps engine.paused() bound to
    // ui->menuActive(). The bypass path leaves UIScreen::None set in
    // the UISystem constructor — gameplay starts immediately, same as
    // every M1..M5 milestone.
    if (game.uiSystem() && !cliMenuBypass) {
        game.uiSystem()->setCurrent(tou2d::UIScreen::MainMenu);
        engine.setPaused(true);
        std::printf("[tou2d] no CLI args — opening MainMenu\n");
    }

    // M5.4 — wire the replay player into the input system once onSetup
    // has run. Recorder opens here too so the file's header lands
    // before any tick is captured.
    tou2d::ReplayRecorder replayRecorder;
    if (!playPath.empty() && game.inputSystem()) {
        game.inputSystem()->setReplayPlayer(&replayPlayer);
    }
    if (!recordPath.empty()) {
        const std::uint8_t mode =
            (matchMode == tou2d::MatchMode::LastShipStanding) ? 1u : 0u;
        // M5.5 — pass the gen config (if active) into the recorder so
        // the v2 header captures (seed, level, density, perim). When
        // gen is off the optional stays empty and recorder writes
        // useGen=0 with the levelDir string.
        const std::optional<tou2d::ProceduralLevelConfig> genForHeader =
            useGen ? std::optional<tou2d::ProceduralLevelConfig>{genCfg}
                   : std::nullopt;
        if (!replayRecorder.open(recordPath, numHumans, numBots, mode,
                                 levelDir, genForHeader, specialKind)) {
            std::fprintf(stderr,
                "[tou2d] --record %s: open failed\n", recordPath.c_str());
            engine.shutdown();
            glfwDestroyWindow(window);
            glfwTerminate();
            return 1;
        }
        std::printf("[tou2d] --record %s: header %u humans + %u bots, mode=%u, useGen=%u\n",
                    recordPath.c_str(),
                    unsigned(numHumans), unsigned(numBots), unsigned(mode),
                    unsigned(useGen));
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

            // 2026-05-28 (rev 2) — narrow the bleed from +2 → +1 px and
            // restore the legacy M3.4 edge-fade. The wider +2 bleed felt
            // too aggressive on the now-denser 4 px grid (overwrote the
            // outline of neighbouring still-alive tiles); a single
            // pixel of overlap is enough to cover the seam residue, and
            // the soft fade reads more like rocky scarring than a hard
            // void rectangle.
            constexpr int kDestructionBleed = 1;
            const int rawX0 = px0 - kDestructionBleed;
            const int rawY0 = py0 - kDestructionBleed;
            const int rawX1 = px0 + tilePx + kDestructionBleed;
            const int rawY1 = py0 + tilePx + kDestructionBleed;
            const int clampedX0 = std::max(0, rawX0);
            const int clampedY0 = std::max(0, rawY0);
            const int clampedX1 = std::min(width,  rawX1);
            const int clampedY1 = std::min(height, rawY1);
            if (clampedX1 <= clampedX0 || clampedY1 <= clampedY0) return;

            // Anti-chunkiness paint (re-landed). Per-pixel darkening
            // envelope: chebyshev distance from rect edge → fade
            // strength (edges keep ~25% of source; centers go near
            // pure void). xor-hash jitter (±10%) breaks up the flat
            // fill so the patch reads as rough rock instead of a clean
            // black rectangle.
            const int   rectW   = clampedX1 - clampedX0;
            const int   rectH   = clampedY1 - clampedY0;
            const float half    = static_cast<float>(std::min(rectW, rectH)) * 0.5f;
            const float invHalf = half > 0.0f ? 1.0f / half : 1.0f;
            for (int y = clampedY0; y < clampedY1; ++y) {
                const int dyEdge = std::min(y - clampedY0, clampedY1 - 1 - y);
                for (int x = clampedX0; x < clampedX1; ++x) {
                    const int dxEdge = std::min(x - clampedX0, clampedX1 - 1 - x);
                    const int edge   = std::min(dxEdge, dyEdge);
                    const float t    = std::min(1.0f, static_cast<float>(edge) * invHalf);

                    const std::uint32_t h =
                        (static_cast<std::uint32_t>(x) * 0x9E3779B1u) ^
                        (static_cast<std::uint32_t>(y) * 0x85EBCA77u);
                    const float jitter =
                        (static_cast<float>(h >> 24) / 255.0f - 0.5f) * 0.2f;
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

            // M4.8 — size the sprite compositor's foreground buffer to
            // match the background. World rect is identical so
            // foreground pixels land 1:1 over background pixels.
            compositor.resize(paddedW, paddedH,
                              halfX * tilePx,
                              halfY * tilePx,
                              tou2d::kWorldUnitsPerImagePixel);
            {
                const bool fgOk = renderer->setForegroundFromRgba(
                    compositor.pixels(),
                    static_cast<std::uint32_t>(paddedW),
                    static_cast<std::uint32_t>(paddedH));
                renderer->setForegroundWorldExtent(
                    (maxWorldX - minWorldX) * 0.5f,
                    (maxWorldY - minWorldY) * 0.5f,
                    (maxWorldX + minWorldX) * 0.5f,
                    (maxWorldY + minWorldY) * 0.5f);
                std::printf("[tou2d] foreground sprite layer installed=%d\n",
                            int(fgOk));
            }

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
    std::uint64_t hashMismatches = 0;
    std::vector<std::uint8_t> spriteUploadScratch;
    std::array<tou2d::PlayerInput, tou2d::kReplayKeyboardSlots> sampledInputs{};
    while (!glfwWindowShouldClose(window) && !engine.quitRequested()) {
        glfwPollEvents();
        // M6.1 — pump UI key edges + dispatch when a menu is up. Drives
        // moveFocus / acceptFocused on UiUp / UiDown / UiAccept.
        // UiCancel returns to MainMenu from sub-screens; on MainMenu
        // it's a no-op (Quit row is the explicit exit). Bypass: when
        // current() == None the poller still updates prev[] but takes
        // no action — so cancelling out of a menu doesn't auto-fire
        // UiAccept on the first gameplay frame.
        auto* ui = game.uiSystem();
        if (ui) {
            uiEdges.poll(window, uiKeyMap);
            if (ui->menuActive()) {
                if (uiEdges.rising[0]) ui->moveFocus(-1);
                if (uiEdges.rising[1]) ui->moveFocus(+1);
                if (uiEdges.rising[4]) ui->acceptFocused();
                if (uiEdges.rising[5] && ui->current() != tou2d::UIScreen::MainMenu) {
                    ui->setCurrent(tou2d::UIScreen::MainMenu);
                }
            }
            // Bind engine pause to menu-active. Cheap conditional set
            // — Engine::setPaused doesn't fire change events, so a
            // no-op call is free.
            const bool wantPaused = ui->menuActive();
            if (engine.paused() != wantPaused) {
                engine.setPaused(wantPaused);
            }
            if (ui->pendingQuit()) {
                std::printf("[tou2d] menu Quit selected — exiting\n");
                ui->clearPendingQuit();
                break;
            }
        }

        // M5.4 — playback advances the recorded stream BEFORE step so
        // InputSystem reads the current tick's inputs from the player.
        // EOF ends the run cleanly.
        if (replayPlayer.valid()) {
            if (!replayPlayer.advance()) {
                std::printf("[tou2d] --play EOF at tick=%llu (read=%llu)\n",
                            static_cast<unsigned long long>(tick),
                            static_cast<unsigned long long>(replayPlayer.ticksRead() - 1));
                break;
            }
        }
        // M5.4 — recording samples the SAME keyboard state InputSystem
        // is about to read (glfwGetKey returns state frozen since
        // pollEvents). Capture before step; write after, paired with
        // the post-step commitHash.
        if (replayRecorder.valid()) {
            for (std::uint8_t s = 0; s < tou2d::kReplayKeyboardSlots; ++s) {
                sampledInputs[s] = tou2d::readKeyboardSlot(window, s);
            }
        }
        engine.step();
        if (replayRecorder.valid()) {
            replayRecorder.recordTick(
                std::span<const tou2d::PlayerInput>(sampledInputs.data(),
                                                    sampledInputs.size()),
                engine.stats().commitHash);
        }
        if (replayPlayer.valid()) {
            const std::uint64_t got = engine.stats().commitHash;
            const std::uint64_t want = replayPlayer.commitHash();
            if (got != want) {
                if (hashMismatches < 4) {
                    std::fprintf(stderr,
                        "[tou2d] --play hash mismatch tick=%llu got=0x%016llx want=0x%016llx\n",
                        static_cast<unsigned long long>(tick),
                        static_cast<unsigned long long>(got),
                        static_cast<unsigned long long>(want));
                }
                ++hashMismatches;
            }
        }
        // M6.1 — repaint the UI overlay bitmap for this tick. Three
        // paths:
        //   * font invalid → no overlay (graceful degrade).
        //   * menuActive → paint the active screen's rows + focus.
        //   * !menuActive → clear to transparent (gameplay tint).
        // The dirty-bbox upload path is the same for all three so the
        // GPU texture always reflects exactly what's in the bitmap.
        if (fontAtlas.valid()) {
            uiOverlay.clear(0u);  // transparent
            if (ui && ui->menuActive()) {
                // Title line — paints which screen the user is on.
                const char* title = "TOU2D";
                switch (ui->current()) {
                    case tou2d::UIScreen::MainMenu: title = "TOU2D \xB7 Main Menu";       break;
                    case tou2d::UIScreen::Credits:  title = "TOU2D \xB7 Credits";          break;
                    default:                        title = "TOU2D \xB7 Menu";             break;
                }
                const int lineH = (fontAtlas.ascent - fontAtlas.descent +
                                   fontAtlas.lineGap);
                float baseY = static_cast<float>(fontAtlas.ascent + 8);
                uiOverlay.drawText(fontAtlas,
                                   /*baseX=*/ 16.0f, baseY,
                                   /*color=*/ 0xFFFFFFFFu, title);
                baseY += static_cast<float>(lineH) * 1.5f;

                const auto rs = ui->currentRows();
                const std::int32_t focus = ui->focusIndex();
                for (std::size_t i = 0; i < rs.size(); ++i) {
                    const bool focused = (static_cast<std::int32_t>(i) == focus);
                    // 0xAABBGGRR — focused=cyan, enabled=white,
                    // disabled=mid-grey. The leading "> " marker is
                    // the second focus cue (colorblind-safe).
                    const std::uint32_t color =
                        focused             ? 0xFFFFFF40u :   // cyan
                        rs[i].enabled       ? 0xFFFFFFFFu :   // white
                                              0xFF808080u;    // grey
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "%s %s",
                                  focused ? ">" : " ", rs[i].label);
                    uiOverlay.drawText(fontAtlas,
                                       /*baseX=*/ 24.0f, baseY,
                                       color, buf);
                    baseY += static_cast<float>(lineH);
                }
            }
            if (!uiOverlayInstalled) {
                const bool ok = renderer->setUiOverlayFromRgba(
                    uiOverlay.bytes(), uiOverlay.width(), uiOverlay.height());
                uiOverlayInstalled = ok;
                (void)uiOverlay.consumeDirty();  // first frame went via setFrom*
                if (!ok) {
                    std::fprintf(stderr,
                        "[tou2d] setUiOverlayFromRgba FAILED — overlay disabled\n");
                }
            } else {
                const auto rect = uiOverlay.consumeDirty();
                if (!rect.empty()) {
                    const std::size_t need = static_cast<std::size_t>(rect.w) *
                                             static_cast<std::size_t>(rect.h) * 4u;
                    if (uiUploadScratch.size() < need) {
                        uiUploadScratch.assign(need, 0);
                    }
                    if (uiOverlay.extractRegion(
                            rect,
                            std::span<std::uint8_t>(uiUploadScratch.data(), need))) {
                        renderer->updateUiOverlayRegion(
                            rect.x, rect.y, rect.w, rect.h,
                            std::span<const std::uint8_t>(uiUploadScratch.data(),
                                                          need));
                    }
                }
            }
        }
        // Flush any per-tick background dirty rect once after the
        // tick's commits — the painter accumulated paint events from
        // BulletTerrain + Collision destroy callbacks; one
        // `updateBackgroundRegion` covers the union bbox.
        bgPainter.flush();
        // M4.8 — composite sprites into the foreground texture for
        // every ship that's alive + has a SpriteRef. The dirty bbox
        // unions every ship's prev + current bbox so a single
        // `updateForegroundRegion` upload covers all four player
        // sprites at once.
        compositor.tick(engine.world(), game.userComponentIds());
        {
            std::int32_t rx, ry, rw, rh;
            if (compositor.consumeDirty(rx, ry, rw, rh)) {
                compositor.copyRegion(rx, ry, rw, rh, spriteUploadScratch);
                renderer->updateForegroundRegion(
                    static_cast<std::uint32_t>(rx),
                    static_cast<std::uint32_t>(ry),
                    static_cast<std::uint32_t>(rw),
                    static_cast<std::uint32_t>(rh),
                    std::span<const std::uint8_t>(spriteUploadScratch.data(),
                                                  spriteUploadScratch.size()));
            }
        }
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

    // M5.4 — flush recorder / report playback verdict before teardown.
    if (replayRecorder.valid()) {
        std::printf("[tou2d] --record wrote %llu ticks to %s\n",
                    static_cast<unsigned long long>(replayRecorder.ticksWritten()),
                    recordPath.c_str());
        replayRecorder.close();
    }
    if (replayPlayer.valid()) {
        std::printf("[tou2d] --play %s — %llu ticks verified, %llu hash mismatches\n",
                    playPath.c_str(),
                    static_cast<unsigned long long>(replayPlayer.ticksRead()),
                    static_cast<unsigned long long>(hashMismatches));
        replayPlayer.close();
    }

    // M5.4 — drop replay pointer before shutdown so onTeardown's input_
    // nulling doesn't leave a dangling borrowed pointer in the system.
    if (!playPath.empty() && game.inputSystem()) {
        game.inputSystem()->setReplayPlayer(nullptr);
    }

    engine.shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();
    return hashMismatches == 0 ? 0 : 3;
}
