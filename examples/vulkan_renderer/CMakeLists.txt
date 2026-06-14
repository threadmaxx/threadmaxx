# Vulkan reference renderer — §3.1 batch 9. Opt-in: silently skipped when
# the Vulkan SDK / GLFW / glslc are not all available on the host. Treated
# as an example (lives outside the core library) but ships as a reusable
# static library so batch 10's RPG demo can link against it.

find_package(Vulkan QUIET)
find_package(glfw3 QUIET)
find_program(THREADMAXX_GLSLC NAMES glslc)

if (NOT Vulkan_FOUND OR NOT glfw3_FOUND OR NOT THREADMAXX_GLSLC)
    if (NOT Vulkan_FOUND)
        message(STATUS "Vulkan SDK not found — skipping vulkan_renderer example")
    elseif (NOT glfw3_FOUND)
        message(STATUS "GLFW3 not found — skipping vulkan_renderer example")
    else()
        message(STATUS "glslc not found in PATH — skipping vulkan_renderer example")
    endif()
    return()
endif()

set(VK_RENDERER_SHADERS
    opaque.vert
    opaque.frag
    opaque_skinned.vert
    debug_line.vert
    debug_line.frag
    debug_point.vert
    debug_point.frag
    background.vert
    background.frag
)

set(VK_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${VK_GENERATED_DIR})

# One custom command per shader: glsl → spirv → embedded byte array header
set(VK_SPIRV_HEADERS "")
foreach (shader IN LISTS VK_RENDERER_SHADERS)
    set(src ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader})
    set(spv ${VK_GENERATED_DIR}/${shader}.spv)
    string(REPLACE "." "_" sym ${shader})
    set(hdr ${VK_GENERATED_DIR}/${sym}_spv.hpp)

    add_custom_command(
        OUTPUT ${spv}
        COMMAND ${THREADMAXX_GLSLC} --target-env=vulkan1.3 -O ${src} -o ${spv}
        DEPENDS ${src}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "glslc ${shader} -> ${shader}.spv"
        VERBATIM
    )
    add_custom_command(
        OUTPUT ${hdr}
        COMMAND ${CMAKE_COMMAND}
                -DSPV_INPUT=${spv}
                -DSPV_OUTPUT=${hdr}
                -DSPV_SYMBOL=k_${sym}_spv
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedSpv.cmake
        DEPENDS ${spv} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedSpv.cmake
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Embedding ${shader}.spv"
        VERBATIM
    )
    list(APPEND VK_SPIRV_HEADERS ${hdr})
endforeach()

add_custom_target(threadmaxx_vk_shaders ALL DEPENDS ${VK_SPIRV_HEADERS})

# --------------------------------------------------------------------------
# Static library: threadmaxx_vulkan_renderer
# --------------------------------------------------------------------------
add_library(threadmaxx_vulkan_renderer STATIC
    src/VulkanContext.cpp
    src/VulkanSwapchain.cpp
    src/VulkanFrameRing.cpp
    src/VulkanPipelines.cpp
    src/VulkanRenderer.cpp
    src/MeshLoader.cpp
    src/TextureLoader.cpp
    src/ShaderLoader.cpp
)
add_library(threadmaxx::vulkan_renderer ALIAS threadmaxx_vulkan_renderer)
add_dependencies(threadmaxx_vulkan_renderer threadmaxx_vk_shaders)

target_include_directories(threadmaxx_vulkan_renderer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${VK_GENERATED_DIR}
)

target_link_libraries(threadmaxx_vulkan_renderer
    PUBLIC
        threadmaxx::threadmaxx
        Vulkan::Vulkan
        glfw
)

# 2026-06-15 — opt-in Dear ImGui integration. When the editor's
# FetchContent block already pulled `imgui`, compile its Vulkan + GLFW
# backend translation units into THIS library and define
# `THREADMAXX_VK_HAS_IMGUI=1` so VulkanRenderer.cpp lights up its
# ImGui submit path. Without the editor's fetch the renderer compiles
# cleanly without ImGui (legacy behaviour).
if (TARGET imgui)
    # imgui_impl_vulkan.cpp + imgui_impl_glfw.cpp live in the
    # FetchContent source dir's `backends/` directory. The root
    # CMakeLists.txt publishes `THREADMAXX_IMGUI_SOURCE_DIR` as a
    # CACHE INTERNAL var; falling back to the directly-scoped
    # `threadmaxx_imgui_SOURCE_DIR` in case this file is included
    # right next to the FetchContent block.
    set(_VK_IMGUI_DIR "")
    if (DEFINED THREADMAXX_IMGUI_SOURCE_DIR)
        set(_VK_IMGUI_DIR "${THREADMAXX_IMGUI_SOURCE_DIR}")
    elseif (DEFINED threadmaxx_imgui_SOURCE_DIR)
        set(_VK_IMGUI_DIR "${threadmaxx_imgui_SOURCE_DIR}")
    else()
        message(FATAL_ERROR
            "[vulkan_renderer] imgui target present but its source dir is unset")
    endif()
    target_sources(threadmaxx_vulkan_renderer PRIVATE
        ${_VK_IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
        ${_VK_IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    )
    target_include_directories(threadmaxx_vulkan_renderer PRIVATE
        ${_VK_IMGUI_DIR}/backends
    )
    target_link_libraries(threadmaxx_vulkan_renderer PUBLIC imgui)
    target_compile_definitions(threadmaxx_vulkan_renderer PUBLIC
        THREADMAXX_VK_HAS_IMGUI=1)
    # ImGui's backends are not strict-warning-clean.
    if (NOT MSVC)
        set_source_files_properties(
            ${_VK_IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
            ${_VK_IMGUI_DIR}/backends/imgui_impl_glfw.cpp
            PROPERTIES COMPILE_OPTIONS
            "-Wno-old-style-cast;-Wno-conversion;-Wno-sign-conversion;-Wno-shadow;-Wno-cast-qual")
    endif()
endif()

# §3.11 batch 9b.3 — bake the per-build .spv directory so the
# `ShaderLoader` can re-read shader bytecode from disk when a hot
# reload fires. Without this the renderer would only see the embedded
# SPIR-V baked into the *_spv.hpp headers and `markResourceStale` would
# be a no-op for any in-place shader edit. PRIVATE: only the renderer
# implementation reads it; the public header stays path-free.
target_compile_definitions(threadmaxx_vulkan_renderer PRIVATE
    THREADMAXX_VK_SHADER_DIR="${VK_GENERATED_DIR}")

set_target_properties(threadmaxx_vulkan_renderer PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# --------------------------------------------------------------------------
# Build-verification smoke binary. Opens a window, runs N ticks, exits.
# Not a demo scene — batch 10 (RPG demo) is the showcase.
# --------------------------------------------------------------------------
add_executable(threadmaxx_vulkan_smoke smoke/main.cpp)
target_link_libraries(threadmaxx_vulkan_smoke PRIVATE threadmaxx::vulkan_renderer)
set_target_properties(threadmaxx_vulkan_smoke PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
