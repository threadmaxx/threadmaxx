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
    BotControlSystem.cpp
    HudSystem.cpp
    RoundRestartSystem.cpp
)

target_include_directories(tou2d_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tou2d_core PUBLIC threadmaxx::threadmaxx glfw)

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
