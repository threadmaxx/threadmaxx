# 3D RPG demo — §3.2 batch 10. Built on top of the Vulkan renderer
# (examples/vulkan_renderer/). If the renderer was skipped at configure
# time (Vulkan / glfw3 / glslc missing), this example is silently
# skipped too — there is no other backend it can fall back to.

if (NOT TARGET threadmaxx::vulkan_renderer)
    message(STATUS "vulkan_renderer target not available — skipping rpg_demo")
    return()
endif()

add_executable(threadmaxx_rpg_demo
    main.cpp
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
)

target_link_libraries(threadmaxx_rpg_demo PRIVATE threadmaxx::vulkan_renderer)

set_target_properties(threadmaxx_rpg_demo PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
