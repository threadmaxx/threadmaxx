# tou2d — 2D Tunnels-of-the-Underworld adaptation.
#
# Milestone 1 (M1): renderer foundation + single-ship thrust loop. Reuses
# the threadmaxx Vulkan renderer with an orthographic camera. The 2D
# game is rendered as ortho-projected unit cubes against the auto
# `instances` lane until M2 swaps in real sprites.
#
# Silently skipped when the Vulkan renderer was not built (Vulkan SDK /
# GLFW / glslc unavailable — see examples/vulkan_renderer/CMakeLists.txt).

if (NOT TARGET threadmaxx::vulkan_renderer)
    message(STATUS "vulkan_renderer target not available — skipping tou2d")
    return()
endif()

# ---- tou2d_core (static library) -----------------------------------------
#
# Carries game-side systems + game wiring. No Vulkan dependency — the
# renderer is plumbed in by main.cpp. Keeps the door open for a headless
# test suite later (mirrors the rpg_demo split).
add_library(tou2d_core STATIC
    TouGame.cpp
    InputSystem.cpp
    MovementSystem.cpp
    CameraSystem.cpp
    TerrainCollisionSystem.cpp
    LevelLoader.cpp
    WeaponFireSystem.cpp
    ProjectileSystem.cpp
    BulletTerrainSystem.cpp
    BulletShipCollisionSystem.cpp
    ShipLifecycleSystem.cpp
    ParticleSystem.cpp
    BotControlSystem.cpp
    HudSystem.cpp
    RoundRestartSystem.cpp
    AudioSystem.cpp
    Replay.cpp
    RepairPickupSystem.cpp
    BulletHomingSystem.cpp
    # M6.0 — UI prereqs (TTF font + text layout). The font asset lives
    # at the repo's root-level `assets/ui/font.ttf` (mirroring the
    # shared `assets/ships`, `assets/sfx`, `assets/music` layout). No
    # CMake-side copy is needed — tou2d reads from `./assets/` at
    # runtime per the existing M4.8 convention.
    ui/Font.cpp
    ui/TextPrintf.cpp
    # M6.0b — UI overlay CPU compositor + UISystem screen-state-machine
    # skeleton. UiOverlayBitmap is the CPU-side RGBA8 framebuffer
    # consumed by VulkanRenderer's new setUiOverlayFromRgba /
    # updateUiOverlayRegion (one upload per tick); UISystem owns
    # `UIScreen current` and emits UIScreenChanged on transition.
    ui/UiOverlayBitmap.cpp
    UISystem.cpp
)

target_include_directories(tou2d_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tou2d_core PUBLIC threadmaxx::threadmaxx glfw)

# M4.8 — AudioSystem.cpp pulls in miniaudio (system header at
# /usr/include/miniaudio/miniaudio.h). Per its docs the Linux build
# needs `-ldl -lpthread -lm`. miniaudio is the only TU that
# `#define MINIAUDIO_IMPLEMENTATION`s so the symbol is unique to
# tou2d_core; no header bleed into other targets.
find_package(Threads REQUIRED)
target_link_libraries(tou2d_core PUBLIC ${CMAKE_DL_LIBS} Threads::Threads m)

set_target_properties(tou2d_core PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# ---- threadmaxx_tou2d (executable) ---------------------------------------
add_executable(threadmaxx_tou2d main.cpp)
target_link_libraries(threadmaxx_tou2d PRIVATE
    tou2d_core
    threadmaxx::vulkan_renderer
)
set_target_properties(threadmaxx_tou2d PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# ---- tou2d_import_lev (Tier-1 .lev importer CLI) ------------------------
# Standalone — no engine / no Vulkan / no GLFW. Builds wherever the rest
# of the project builds; runs offline to unpack <foo>.lev into a
# directory the runtime loader (M2.7) can consume.
add_executable(tou2d_import_lev tou2d_import_lev.cpp)
set_target_properties(tou2d_import_lev PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# ---- tou2d_import_shp (Tier-2 .SHP importer CLI) ------------------------
# Same shape as tou2d_import_lev: standalone, no engine deps. Decodes
# the display name + stat triplet from a TOU ship file and stages
# header.bin / body.bin / config.txt under <outdir>/<stem>/ . The
# sprite section is left opaque pending reverse-engineering; this CLI
# is enough to wire imported ship names into a future loader.
add_executable(tou2d_import_shp tou2d_import_shp.cpp)
set_target_properties(tou2d_import_shp PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
