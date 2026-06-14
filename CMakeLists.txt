cmake_minimum_required(VERSION 3.20)
project(threadmaxx
    VERSION 1.2.1
    DESCRIPTION "Graphics-independent C++20 game backend with a job system"
    LANGUAGES CXX)

# --------------------------------------------------------------------------
# Options
# --------------------------------------------------------------------------
option(THREADMAXX_BUILD_EXAMPLES "Build the bundled minimal example" ON)
option(THREADMAXX_BUILD_TESTS "Build and register the test suite" ON)
option(THREADMAXX_BUILD_BENCHMARKS "Build the standalone benchmark binaries" OFF)
option(THREADMAXX_BUILD_SIMD "Make the threadmaxx_simd header-only sibling library available" ON)
option(THREADMAXX_BUILD_ANIMATION "Make the threadmaxx_animation sibling library available" ON)
option(THREADMAXX_BUILD_PHYSICS "Make the threadmaxx_physics sibling library available" ON)
option(THREADMAXX_BUILD_NAVMESH "Make the threadmaxx_navmesh sibling library available" ON)
option(THREADMAXX_BUILD_AUDIO "Make the threadmaxx_audio sibling library available" ON)
option(THREADMAXX_BUILD_UI "Make the threadmaxx_ui sibling library available" ON)
option(THREADMAXX_BUILD_INPUT "Make the threadmaxx_input sibling library available" ON)
option(THREADMAXX_BUILD_ASSETS "Make the threadmaxx_assets sibling library available" ON)
option(THREADMAXX_BUILD_REFLECT "Make the threadmaxx_reflect sibling library available" ON)
option(THREADMAXX_BUILD_EDITOR "Make the threadmaxx_editor sibling library available" ON)
option(THREADMAXX_EDITOR_FETCH_IMGUI "FetchContent Dear ImGui and build the ImGui editor backend (E11)" OFF)
option(THREADMAXX_BUILD_NETWORK "Make the threadmaxx_network sibling library available" ON)
option(THREADMAXX_BUILD_STUDIO "Make the threadmaxx_studio sibling library available" ON)
option(THREADMAXX_BUILD_MIGRATION "Make the threadmaxx_migration sibling library available" ON)
option(THREADMAXX_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

# Default install rules ON when threadmaxx is the top-level project; OFF when
# pulled in via add_subdirectory so consumers don't accidentally install us.
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(THREADMAXX_INSTALL_DEFAULT ON)
else()
    set(THREADMAXX_INSTALL_DEFAULT OFF)
endif()
option(THREADMAXX_INSTALL "Generate install rules for threadmaxx" ${THREADMAXX_INSTALL_DEFAULT})

# --------------------------------------------------------------------------
# Library
# --------------------------------------------------------------------------
set(THREADMAXX_PUBLIC_HEADERS
    include/threadmaxx/threadmaxx.hpp
    include/threadmaxx/Config.hpp
    include/threadmaxx/Handles.hpp
    include/threadmaxx/Components.hpp
    include/threadmaxx/CommandBuffer.hpp
    include/threadmaxx/UserComponent.hpp
    include/threadmaxx/EventChannel.hpp
    include/threadmaxx/Logger.hpp
    include/threadmaxx/Query.hpp
    include/threadmaxx/RenderFrame.hpp
    include/threadmaxx/Resource.hpp
    include/threadmaxx/ScratchArena.hpp
    include/threadmaxx/SkipPolicy.hpp
    include/threadmaxx/Serialization.hpp
    include/threadmaxx/Stats.hpp
    include/threadmaxx/System.hpp
    include/threadmaxx/TaskTag.hpp
    include/threadmaxx/Renderer.hpp
    include/threadmaxx/Game.hpp
    include/threadmaxx/SpatialHash.hpp
    include/threadmaxx/Trace.hpp
    include/threadmaxx/Telemetry.hpp
    include/threadmaxx/version.hpp
    include/threadmaxx/World.hpp
    include/threadmaxx/Engine.hpp
)

set(THREADMAXX_SOURCES
    src/Archetype.cpp
    src/EntityStorage.cpp
    src/JobSystem.cpp
    src/CommandBuffer.cpp
    src/World.cpp
    src/Engine.cpp
    src/EngineImpl.cpp
    src/HierarchySystem.cpp
    src/ResourceRegistry.cpp
    src/ScratchArena.cpp
    src/Trace.cpp
    src/Telemetry.cpp
    src/UserComponentRegistry.cpp
)

add_library(threadmaxx STATIC ${THREADMAXX_SOURCES} ${THREADMAXX_PUBLIC_HEADERS})
add_library(threadmaxx::threadmaxx ALIAS threadmaxx)

target_include_directories(threadmaxx
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(threadmaxx PUBLIC cxx_std_20)

set_target_properties(threadmaxx PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# Threads (std::thread)
find_package(Threads REQUIRED)
target_link_libraries(threadmaxx PUBLIC Threads::Threads)

if (MSVC)
    target_compile_options(threadmaxx PRIVATE /W4 /permissive-)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx PRIVATE /WX)
    endif()
else()
    target_compile_options(threadmaxx PRIVATE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wnon-virtual-dtor -Wold-style-cast
        -Wcast-align -Wunused -Woverloaded-virtual
        -Wconversion -Wsign-conversion -Wdouble-promotion
    )
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx PRIVATE -Werror)
    endif()
endif()

# --------------------------------------------------------------------------
# threadmaxx_simd — header-only sibling library
# --------------------------------------------------------------------------
#
# Opt-in via `-DTHREADMAXX_BUILD_SIMD=ON` (the default). The library
# is pure headers; the INTERFACE target carries only the include
# directory. Consumers `target_link_libraries(... threadmaxx::simd)`.
# Designed to coexist with the core engine — the engine does not
# depend on it. See `include/threadmaxx_simd/DESIGN_NOTES.md` for
# the spec and `include/threadmaxx_simd/FUTURE_WORK.md` for the
# batch plan.
if (THREADMAXX_BUILD_SIMD)
    add_library(threadmaxx_simd INTERFACE)
    add_library(threadmaxx::simd ALIAS threadmaxx_simd)
    target_include_directories(threadmaxx_simd INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_compile_features(threadmaxx_simd INTERFACE cxx_std_20)
    # The SIMD library reuses the core's POD types (`Vec3`, `Transform`,
    # etc.) so consumers need both. Marked PUBLIC so a single
    # `target_link_libraries(client PRIVATE threadmaxx::simd)` is
    # enough.
    target_link_libraries(threadmaxx_simd INTERFACE threadmaxx::threadmaxx)
endif()

# --------------------------------------------------------------------------
# threadmaxx_animation — skeletal pose / clip / IK sibling library
# --------------------------------------------------------------------------
#
# Opt-in via `-DTHREADMAXX_BUILD_ANIMATION=ON` (the default). Static
# library — public headers under `include/threadmaxx_animation/`,
# implementation under `src/threadmaxx_animation/`. The library
# reuses the core's `Vec3` / `Quat` POD types and optionally borrows
# from `threadmaxx::simd` for vectorized pose blending (v1.x). See
# `include/threadmaxx_animation/DESIGN_NOTES.md` for the spec and
# `FUTURE_WORK.md` for the batch plan.
if (THREADMAXX_BUILD_ANIMATION)
    add_subdirectory(src/threadmaxx_animation)
endif()

# --------------------------------------------------------------------------
# threadmaxx_physics — backend-adapter physics sibling library
# --------------------------------------------------------------------------
#
# Opt-in via `-DTHREADMAXX_BUILD_PHYSICS=ON` (the default). Static
# library — public headers under `include/threadmaxx_physics/`,
# implementation under `src/threadmaxx_physics/`. v1.0 ships with the
# bundled `StubBackend` (deterministic, no real physics) as the
# documented null implementation; the Jolt adapter lands in batch P9
# behind `find_package(Jolt)`. See
# `include/threadmaxx_physics/DESIGN_NOTES.md` for the spec and
# `FUTURE_WORK.md` for the batch plan.
if (THREADMAXX_BUILD_PHYSICS)
    add_subdirectory(src/threadmaxx_physics)
endif()

# --------------------------------------------------------------------------
# threadmaxx_navmesh — navmesh + pathfinding sibling library
# --------------------------------------------------------------------------
#
# Opt-in via `-DTHREADMAXX_BUILD_NAVMESH=ON` (the default). Static
# library — public headers under `include/threadmaxx_navmesh/`,
# implementation under `src/threadmaxx_navmesh/`. v1.0 sequences as
# N1 → N9 per `include/threadmaxx_navmesh/FUTURE_WORK.md`. Reuses
# the core's `Vec3` POD; no other engine dependency. See
# `include/threadmaxx_navmesh/DESIGN_NOTES.md` for the spec.
if (THREADMAXX_BUILD_NAVMESH)
    add_subdirectory(src/threadmaxx_navmesh)
endif()

# --------------------------------------------------------------------------
# threadmaxx_audio — audio mixing + 3D audio sibling library
# --------------------------------------------------------------------------
#
# Opt-in via `-DTHREADMAXX_BUILD_AUDIO=ON` (the default). Static library —
# public headers under `include/threadmaxx_audio/`, implementation under
# `src/threadmaxx_audio/` (platform backends under `backends/`). v1.0
# sequences as AU1 → AU8 per `include/threadmaxx_audio/FUTURE_WORK.md`. No
# engine dependency at link time (header-only POD math; the engine reuses
# this library, not the other way around). See
# `include/threadmaxx_audio/DESIGN_NOTES.md` for the spec.
if (THREADMAXX_BUILD_AUDIO)
    add_subdirectory(src/threadmaxx_audio)
endif()

# --------------------------------------------------------------------------
# threadmaxx_ui — immediate-mode UI primitives sibling library
# --------------------------------------------------------------------------
#
# Opt-in via `-DTHREADMAXX_BUILD_UI=ON` (the default). Static library —
# public headers under `include/threadmaxx_ui/`, implementation under
# `src/threadmaxx_ui/` (renderer adapters under `backends/`). v1.0 sequences
# as UI1 → UI8 per `include/threadmaxx_ui/FUTURE_WORK.md`. Engine-
# independent: depends only on the STL; renderer integration ships as a
# thin draw-list adapter. See `include/threadmaxx_ui/DESIGN_NOTES.md` for
# the spec.
if (THREADMAXX_BUILD_UI)
    add_subdirectory(src/threadmaxx_ui)
endif()

# --------------------------------------------------------------------------
# threadmaxx_input — input + binding sibling library
# --------------------------------------------------------------------------
#
# Opt-in via `-DTHREADMAXX_BUILD_INPUT=ON` (the default). Static library —
# public headers under `include/threadmaxx_input/`, implementation under
# `src/threadmaxx_input/`. v1.0 sequences as I1 → I8 per
# `include/threadmaxx_input/FUTURE_WORK.md`. Engine-independent: depends
# only on the STL; UI bridge (I7) and platform backends (I8) opt in via
# their own flags. See `include/threadmaxx_input/DESIGN_NOTES.md` for the
# spec.
if (THREADMAXX_BUILD_INPUT)
    add_subdirectory(src/threadmaxx_input)
endif()

# --------------------------------------------------------------------------
# threadmaxx_assets — asset import + cache sibling library
# --------------------------------------------------------------------------
#
# Opt-in via `-DTHREADMAXX_BUILD_ASSETS=ON` (the default). Static library —
# public headers under `include/threadmaxx_assets/`, implementation under
# `src/threadmaxx_assets/`. v1.0 sequences as A1 → A8 per
# `include/threadmaxx_assets/FUTURE_WORK.md`. Engine-independent: depends
# only on the STL; the engine bridge (A7) opts in via a `TARGET
# threadmaxx::threadmaxx` check and exports
# `THREADMAXX_ASSETS_HAS_ENGINE_BRIDGE=1`. See
# `include/threadmaxx_assets/DESIGN_NOTES.md` for the spec.
if (THREADMAXX_BUILD_ASSETS)
    add_subdirectory(src/threadmaxx_assets)
endif()

# --------------------------------------------------------------------------
# threadmaxx_reflect — compile-time + runtime reflection sibling library
# --------------------------------------------------------------------------
#
# Opt-in via `-DTHREADMAXX_BUILD_REFLECT=ON` (the default). Static library
# — public headers under `include/threadmaxx_reflect/`, implementation
# under `src/threadmaxx_reflect/`. v1.0 sequences as R1 → R8 per
# `include/threadmaxx_reflect/FUTURE_WORK.md`. Engine-independent: depends
# only on the STL; the engine bridge (R7) opts in via a `TARGET
# threadmaxx::threadmaxx` check and exports
# `THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE=1`. See
# `include/threadmaxx_reflect/DESIGN_NOTES.md` for the spec.
if (THREADMAXX_BUILD_REFLECT)
    add_subdirectory(src/threadmaxx_reflect)
endif()

# --------------------------------------------------------------------------
# threadmaxx_network — networking / replication / rollback sibling library
# --------------------------------------------------------------------------
#
# Opt-in via `-DTHREADMAXX_BUILD_NETWORK=ON` (the default). Static
# library — public headers under `include/threadmaxx_network/`,
# implementation under `src/threadmaxx_network/`. v1.0 sequences as
# NW1 → NW10 per `include/threadmaxx_network/FUTURE_WORK.md`.
# Engine-agnostic (per DESIGN_NOTES §2.1); game code bridges via
# `SimulationBridge` functors.
if (THREADMAXX_BUILD_NETWORK)
    add_subdirectory(src/threadmaxx_network)
endif()

# --------------------------------------------------------------------------
# threadmaxx_editor — editor / tooling / hot-reload sibling library
# --------------------------------------------------------------------------
#
# Opt-in via `-DTHREADMAXX_BUILD_EDITOR=ON` (the default). Static library
# — public headers under `include/threadmaxx_editor/`, implementation
# under `src/threadmaxx_editor/`. v1.0 sequences as E1 → E10 per
# `include/threadmaxx_editor/FUTURE_WORK.md`. Depends on the core
# (PUBLIC link); optionally on threadmaxx::reflect (E7+; exports
# `THREADMAXX_EDITOR_HAS_REFLECT=1`). See
# `include/threadmaxx_editor/DESIGN_NOTES.md` for the spec.
if (THREADMAXX_BUILD_EDITOR)
    # Optional Dear ImGui — fetched only when explicitly opted in.
    # When this target exists, src/threadmaxx_editor compiles the
    # ImGui editor backend (E11) and exports
    # THREADMAXX_EDITOR_HAS_IMGUI_BACKEND=1.
    if (THREADMAXX_EDITOR_FETCH_IMGUI AND NOT TARGET imgui)
        include(FetchContent)
        FetchContent_Declare(threadmaxx_imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG v1.91.5
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(threadmaxx_imgui)
        # Publish the fetched source dir as a globally-visible cache var
        # so a parent build (e.g. gothic_like_threadmaxx pulling
        # examples/vulkan_renderer separately) can locate the
        # imgui_impl_vulkan.cpp / imgui_impl_glfw.cpp backend
        # translation units.
        set(THREADMAXX_IMGUI_SOURCE_DIR "${threadmaxx_imgui_SOURCE_DIR}"
            CACHE INTERNAL "Resolved imgui source directory")
        if (NOT TARGET imgui)
            # imgui's repo doesn't ship a CMakeLists; assemble one.
            add_library(imgui STATIC
                ${threadmaxx_imgui_SOURCE_DIR}/imgui.cpp
                ${threadmaxx_imgui_SOURCE_DIR}/imgui_demo.cpp
                ${threadmaxx_imgui_SOURCE_DIR}/imgui_draw.cpp
                ${threadmaxx_imgui_SOURCE_DIR}/imgui_tables.cpp
                ${threadmaxx_imgui_SOURCE_DIR}/imgui_widgets.cpp
            )
            # SYSTEM include so consumers don't trip our strict warning
            # set on upstream ImGui headers (it has C-style casts in
            # public macros like IM_COL32 / GetCharAdvance / etc).
            target_include_directories(imgui SYSTEM PUBLIC
                ${threadmaxx_imgui_SOURCE_DIR})
            # And keep ImGui's own .cpp build relaxed: it never has
            # to satisfy our strict warning set.
            if (NOT MSVC)
                target_compile_options(imgui PRIVATE
                    -Wno-old-style-cast -Wno-conversion
                    -Wno-sign-conversion)
            endif()
            set_target_properties(imgui PROPERTIES
                POSITION_INDEPENDENT_CODE ON)
        endif()
    endif()
    add_subdirectory(src/threadmaxx_editor)
endif()

# --------------------------------------------------------------------------
# threadmaxx_migration — save versioning / migration pipeline
# --------------------------------------------------------------------------
#
# Opt-in via `-DTHREADMAXX_BUILD_MIGRATION=ON` (the default). Static
# library — public headers under `include/threadmaxx_migration/`,
# implementation under `src/threadmaxx_migration/`. v1.0 sequences as
# M1 → M8 per `include/threadmaxx_migration/FUTURE_WORK.md`.
# Engine-independent — depends only on the STL. M6 introduces an
# opt-in adapter for the engine's WorldSnapshot. Must process before
# threadmaxx_studio so the ST35..ST38 migration panels see the target.
if (THREADMAXX_BUILD_MIGRATION)
    add_subdirectory(src/threadmaxx_migration)
    add_subdirectory(tools/migration_convert)
endif()

# --------------------------------------------------------------------------
# threadmaxx_studio — panel host for the editor / sibling diagnostics
# --------------------------------------------------------------------------
#
# Opt-in via `-DTHREADMAXX_BUILD_STUDIO=ON` (the default). Static library
# — public headers under `include/threadmaxx_studio/`, implementation
# under `src/threadmaxx_studio/`. v1.0 sequences as ST1 → ST41 per
# `include/threadmaxx_studio/FUTURE_WORK.md`. Engine-agnostic core: the
# library itself does NOT link the core engine; concrete data sources
# (DirectDataSource in ST4) opt into the engine in their own TU. The
# editor v1.0 surface is a public dependency from ST2 onward (panels
# render through `editor::IEditorBackend`). See
# `include/threadmaxx_studio/DESIGN_NOTES.md` for the spec.
if (THREADMAXX_BUILD_STUDIO)
    add_subdirectory(src/threadmaxx_studio)
endif()

# --------------------------------------------------------------------------
# Examples
# --------------------------------------------------------------------------
if (THREADMAXX_BUILD_EXAMPLES)
    add_subdirectory(examples/minimal)
    add_subdirectory(examples/boids)
    add_subdirectory(examples/vulkan_renderer)
    add_subdirectory(examples/rpg_demo)
    add_subdirectory(examples/tou2d)
    add_subdirectory(examples/physics_demo)
    add_subdirectory(examples/navmesh_bake)
    add_subdirectory(examples/audio_demo)
    add_subdirectory(examples/ui_demo)
    add_subdirectory(examples/input_demo)
    if (TARGET threadmaxx::assets)
        add_subdirectory(examples/assets_demo)
    endif()
    if (TARGET threadmaxx::reflect)
        add_subdirectory(examples/reflect_demo)
    endif()
    if (TARGET threadmaxx::editor)
        add_subdirectory(examples/editor_demo)
    endif()
    if (TARGET threadmaxx::studio)
        add_subdirectory(examples/studio_demo)
    endif()
endif()

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
if (THREADMAXX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --------------------------------------------------------------------------
# Benchmarks (opt-in: -DTHREADMAXX_BUILD_BENCHMARKS=ON)
# --------------------------------------------------------------------------
if (THREADMAXX_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()

# --------------------------------------------------------------------------
# Install
# --------------------------------------------------------------------------
if (THREADMAXX_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(THREADMAXX_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/threadmaxx")

    install(TARGETS threadmaxx
        EXPORT threadmaxxTargets
        ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY include/threadmaxx
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp"
    )

    # Sibling SIMD library — header-only INTERFACE target. Only exported
    # when THREADMAXX_BUILD_SIMD is ON; otherwise the threadmaxx package
    # ships without it and `if (TARGET threadmaxx::simd)` is the
    # downstream probe.
    if (THREADMAXX_BUILD_SIMD)
        install(TARGETS threadmaxx_simd
            EXPORT threadmaxxTargets
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
        install(DIRECTORY include/threadmaxx_simd
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.hpp"
        )
    endif()

    # Sibling animation library — static. Same `if (TARGET
    # threadmaxx::animation)` downstream probe.
    if (THREADMAXX_BUILD_ANIMATION)
        install(TARGETS threadmaxx_animation
            EXPORT threadmaxxTargets
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
        install(DIRECTORY include/threadmaxx_animation
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.hpp"
        )
    endif()

    # Sibling physics library — static. Same `if (TARGET
    # threadmaxx::physics)` downstream probe.
    if (THREADMAXX_BUILD_PHYSICS)
        install(TARGETS threadmaxx_physics
            EXPORT threadmaxxTargets
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
        install(DIRECTORY include/threadmaxx_physics
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.hpp"
        )
    endif()

    # Sibling navmesh library — static. Same `if (TARGET
    # threadmaxx::navmesh)` downstream probe.
    if (THREADMAXX_BUILD_NAVMESH)
        install(TARGETS threadmaxx_navmesh
            EXPORT threadmaxxTargets
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
        install(DIRECTORY include/threadmaxx_navmesh
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.hpp"
        )
    endif()

    # Sibling audio library — static. Same `if (TARGET threadmaxx::audio)`
    # downstream probe.
    if (THREADMAXX_BUILD_AUDIO)
        install(TARGETS threadmaxx_audio
            EXPORT threadmaxxTargets
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
        install(DIRECTORY include/threadmaxx_audio
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.hpp"
        )
    endif()

    # Sibling UI library — static. Same `if (TARGET threadmaxx::ui)`
    # downstream probe.
    if (THREADMAXX_BUILD_UI)
        install(TARGETS threadmaxx_ui
            EXPORT threadmaxxTargets
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
        install(DIRECTORY include/threadmaxx_ui
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.hpp"
        )
    endif()

    # Sibling input library — static. Same `if (TARGET threadmaxx::input)`
    # downstream probe.
    if (THREADMAXX_BUILD_INPUT)
        install(TARGETS threadmaxx_input
            EXPORT threadmaxxTargets
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
        install(DIRECTORY include/threadmaxx_input
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.hpp"
        )
    endif()

    # Sibling assets library — static. Same `if (TARGET threadmaxx::assets)`
    # downstream probe.
    if (THREADMAXX_BUILD_ASSETS)
        install(TARGETS threadmaxx_assets
            EXPORT threadmaxxTargets
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
        install(DIRECTORY include/threadmaxx_assets
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.hpp"
        )
    endif()

    # Sibling reflect library — static. Same `if (TARGET threadmaxx::reflect)`
    # downstream probe.
    if (THREADMAXX_BUILD_REFLECT)
        install(TARGETS threadmaxx_reflect
            EXPORT threadmaxxTargets
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
        install(DIRECTORY include/threadmaxx_reflect
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.hpp"
        )
    endif()

    # Sibling network library — static. Same `if (TARGET threadmaxx::network)`
    # downstream probe.
    if (THREADMAXX_BUILD_NETWORK)
        install(TARGETS threadmaxx_network
            EXPORT threadmaxxTargets
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
        install(DIRECTORY include/threadmaxx_network
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.hpp"
        )
    endif()

    # Sibling editor library — static. Same `if (TARGET threadmaxx::editor)`
    # downstream probe. NOTE: when THREADMAXX_EDITOR_FETCH_IMGUI=ON the
    # ImGui static lib is baked into the editor archive — it does not
    # install as a separate threadmaxx::imgui target.
    if (THREADMAXX_BUILD_EDITOR)
        install(TARGETS threadmaxx_editor
            EXPORT threadmaxxTargets
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
        install(DIRECTORY include/threadmaxx_editor
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.hpp"
        )
    endif()

    # Sibling migration library — static. Same `if (TARGET threadmaxx::migration)`
    # downstream probe.
    if (THREADMAXX_BUILD_MIGRATION)
        install(TARGETS threadmaxx_migration
            EXPORT threadmaxxTargets
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
        install(DIRECTORY include/threadmaxx_migration
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.hpp"
        )
    endif()

    # Sibling studio library — static. Same `if (TARGET threadmaxx::studio)`
    # downstream probe. PUBLIC-links every sibling whose target exists;
    # those siblings must also be installed (their install blocks above
    # gate on their own THREADMAXX_BUILD_<X> flag).
    if (THREADMAXX_BUILD_STUDIO)
        install(TARGETS threadmaxx_studio
            EXPORT threadmaxxTargets
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
        install(DIRECTORY include/threadmaxx_studio
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            FILES_MATCHING PATTERN "*.hpp"
        )
    endif()

    install(EXPORT threadmaxxTargets
        FILE threadmaxxTargets.cmake
        NAMESPACE threadmaxx::
        DESTINATION ${THREADMAXX_INSTALL_CMAKEDIR}
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/threadmaxxConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/threadmaxxConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/threadmaxxConfig.cmake"
        INSTALL_DESTINATION ${THREADMAXX_INSTALL_CMAKEDIR}
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/threadmaxxConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/threadmaxxConfigVersion.cmake"
        DESTINATION ${THREADMAXX_INSTALL_CMAKEDIR}
    )
endif()

# --------------------------------------------------------------------------
# Documentation
# --------------------------------------------------------------------------
# Optional `doc` target. Only registered when Doxygen is installed — the
# library itself never needs it. Run with:
#     cmake --build build --target doc
# Output lands in <source>/doc/generated/html (path set in Doxyfile so
# editor previewing works whether or not CMake is involved).
find_package(Doxygen QUIET)
if (DOXYGEN_FOUND)
    add_custom_target(doc
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating threadmaxx API documentation with Doxygen"
        VERBATIM)
endif()
