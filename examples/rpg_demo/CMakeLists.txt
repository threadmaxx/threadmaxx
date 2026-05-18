# 3D RPG demo — §3.2 batch 10 + §3.11 batches D1–D3 + D2.
#
# Built on top of the Vulkan renderer (examples/vulkan_renderer/). If
# the renderer was skipped at configure time (Vulkan / glfw3 / glslc
# missing), this example is silently skipped too — there is no other
# backend it can fall back to.
#
# §3.11 batch D-audit (2026-05-16): the systems + game logic are now
# packaged as a static library `rpg_demo_core` so a dedicated headless
# test suite (`tests/rpg_demo/`) can exercise them without GLFW or
# Vulkan in the loop. The executable layer (`main.cpp`) brings in the
# windowing + renderer wiring.

if (NOT TARGET threadmaxx::vulkan_renderer)
    message(STATUS "vulkan_renderer target not available — skipping rpg_demo")
    return()
endif()

# ---- rpg_demo_core (static library) ---------------------------------------
#
# Holds every system + game-logic .cpp. Public deps: threadmaxx + GLFW
# (for the global Input.cpp helper). NO Vulkan dependency — the
# Vulkan renderer is wired in by `main.cpp`, which is the executable's
# job. Tests link this lib without needing a window.
add_library(rpg_demo_core STATIC
    DemoGame.cpp
    Input.cpp
    PlayerInputSystem.cpp
    MovementSystem.cpp
    NPCBrainSystem.cpp
    PickupSystem.cpp
    CameraSystem.cpp
    DayNightSystem.cpp
    CubeRenderSystem.cpp
    DebugOverlaySystem.cpp
    SaveLoadSystem.cpp
    HudSystem.cpp
    # §3.11.1 batch D1 — combat + hierarchy + damage events
    CombatSystem.cpp
    DamageSystem.cpp
    RespawnSystem.cpp
    HealthBarSystem.cpp
    # §3.11.4 batch D4 — quest tracking
    QuestSystem.cpp
    # §3.11.6 batch D6 — procedural animation
    AnimationSystem.cpp
    # §3.11.7 batch D7 — simulated boot-time loader
    PreloadLoader.cpp
    # §3.11 batch 9b.1 — Wavefront OBJ parser (sibling-library)
    ObjLoader.cpp
    # §3.11.7b.5 batch 9b.4.c — procedural 2-bone skinned demo
    SkinnedCapsule.cpp
    SkinnedRenderSystem.cpp
)

target_include_directories(rpg_demo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rpg_demo_core PUBLIC threadmaxx::threadmaxx glfw)

# §3.11 batch 9b.2 — bake the source-dir path so the executable can
# resolve `examples/rpg_demo/assets/*.obj` regardless of the CWD it's
# launched from. `RPG_DEMO_SOURCE_DIR` is a `const char*` string
# literal in main.cpp. Headless tests don't depend on the path.
target_compile_definitions(rpg_demo_core PUBLIC
    RPG_DEMO_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(rpg_demo_core PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# ---- threadmaxx_rpg_demo (executable) -------------------------------------
add_executable(threadmaxx_rpg_demo main.cpp)
target_link_libraries(threadmaxx_rpg_demo PRIVATE
    rpg_demo_core
    threadmaxx::vulkan_renderer
)
set_target_properties(threadmaxx_rpg_demo PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
