set(THREADMAXX_PHYSICS_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_physics/threadmaxx_physics.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_physics/types.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_physics/body.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_physics/shape.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_physics/config.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_physics/backend.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_physics/character.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_physics/constraints.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_physics/contact.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_physics/diagnostics.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_physics/jolt_backend.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_physics/query.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_physics/step.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_physics/stub_backend.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_physics/sync.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_physics/version.hpp
)

set(THREADMAXX_PHYSICS_SOURCES
    backends/StubBackend.cpp
    backends/JoltBackend.cpp
    BackendDefaults.cpp
    CharacterController.cpp
)

# --------------------------------------------------------------------------
# Optional Jolt backend (P9). Two paths to locate Jolt:
#   1. System install: `find_package(Jolt CONFIG QUIET)`. Provides the
#      `Jolt::Jolt` imported target.
#   2. Opt-in FetchContent fallback: configure with
#      `-DTHREADMAXX_PHYSICS_FETCH_JOLT=ON` and we pull a pinned upstream
#      tag and build it as a subproject. Used by CI and by developers
#      who don't want to install Jolt system-wide.
#
# When neither path yields Jolt the `JoltBackend.cpp` translation unit
# still compiles but contains only a stub `makeJoltBackend()` returning
# nullptr — `jolt_backend.hpp::joltBackendAvailable()` reports false and
# the gated tests / bench are not registered with CTest. The library
# stays usable through `StubBackend` alone.
# --------------------------------------------------------------------------
option(THREADMAXX_PHYSICS_FETCH_JOLT
    "When ON and find_package(Jolt) fails, FetchContent a pinned upstream tag"
    OFF)

# Pin matches the v1.0 close-out test gate. Bumping the tag is a
# documented upgrade path: rerun the conformance + smoke tests against
# the new tag, update the per-test tolerances if real-physics drift
# crosses the documented bounds, and bump this line in the same PR.
set(THREADMAXX_PHYSICS_JOLT_TAG "v5.3.0" CACHE STRING
    "Pinned upstream Jolt tag for FetchContent fallback")

set(THREADMAXX_PHYSICS_HAS_JOLT OFF)
set(THREADMAXX_PHYSICS_JOLT_TARGET "")

find_package(Jolt CONFIG QUIET)
if (Jolt_FOUND)
    set(THREADMAXX_PHYSICS_HAS_JOLT ON)
    set(THREADMAXX_PHYSICS_JOLT_TARGET Jolt::Jolt)
    message(STATUS "threadmaxx_physics: using system Jolt (${Jolt_VERSION})")
elseif (THREADMAXX_PHYSICS_FETCH_JOLT)
    include(FetchContent)
    # Jolt v5.x ships its CMakeLists under Build/ rather than the repo
    # root; SOURCE_SUBDIR points FetchContent at the right place.
    FetchContent_Declare(
        JoltPhysics
        GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
        GIT_TAG ${THREADMAXX_PHYSICS_JOLT_TAG}
        SOURCE_SUBDIR Build
        GIT_SHALLOW TRUE)
    # Match the test gate's "single-threaded solver, fixed timestep"
    # determinism profile — see DESIGN_NOTES §5.2 and the P4 test gate.
    set(CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "" FORCE)
    set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
    # Suppress Jolt's bundled samples / tests / hello-world targets so
    # FetchContent doesn't pollute our build with binaries we don't ship.
    set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
    set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
    set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
    set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(JoltPhysics)
    set(THREADMAXX_PHYSICS_HAS_JOLT ON)
    set(THREADMAXX_PHYSICS_JOLT_TARGET Jolt)
    message(STATUS "threadmaxx_physics: FetchContent Jolt ${THREADMAXX_PHYSICS_JOLT_TAG}")
else()
    message(STATUS "threadmaxx_physics: Jolt not found; only StubBackend available "
                   "(set THREADMAXX_PHYSICS_FETCH_JOLT=ON or install Jolt to enable)")
endif()

add_library(threadmaxx_physics STATIC
    ${THREADMAXX_PHYSICS_SOURCES}
    ${THREADMAXX_PHYSICS_PUBLIC_HEADERS}
)
add_library(threadmaxx::physics ALIAS threadmaxx_physics)

target_include_directories(threadmaxx_physics
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(threadmaxx_physics PUBLIC cxx_std_20)

# Reuses threadmaxx::Vec3 / Quat from the core lib — public link so a
# single `target_link_libraries(... threadmaxx::physics)` on a
# consumer pulls in both.
target_link_libraries(threadmaxx_physics PUBLIC threadmaxx::threadmaxx)

# Surface the gate to the parent / sibling directories (tests/, bench/)
# so they can conditionally register Jolt-backed targets.
set(THREADMAXX_PHYSICS_HAS_JOLT ${THREADMAXX_PHYSICS_HAS_JOLT} PARENT_SCOPE)

if (THREADMAXX_PHYSICS_HAS_JOLT)
    target_link_libraries(threadmaxx_physics PRIVATE ${THREADMAXX_PHYSICS_JOLT_TARGET})
    target_compile_definitions(threadmaxx_physics PRIVATE THREADMAXX_PHYSICS_HAS_JOLT=1)
    # Treat Jolt's headers as SYSTEM so our `-Wconversion / -Wshadow /
    # -Wold-style-cast` set doesn't flag every line of Jolt's internals.
    get_target_property(_jolt_inc_dirs ${THREADMAXX_PHYSICS_JOLT_TARGET}
                       INTERFACE_INCLUDE_DIRECTORIES)
    if (_jolt_inc_dirs)
        target_include_directories(threadmaxx_physics SYSTEM PRIVATE ${_jolt_inc_dirs})
    endif()
endif()

set_target_properties(threadmaxx_physics PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if (MSVC)
    target_compile_options(threadmaxx_physics PRIVATE /W4 /permissive-)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_physics PRIVATE /WX)
    endif()
else()
    target_compile_options(threadmaxx_physics PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wsign-conversion -Wconversion -Wold-style-cast)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_physics PRIVATE -Werror)
    endif()
endif()
