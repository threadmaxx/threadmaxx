cmake_minimum_required(VERSION 3.20)
project(threadmaxx
    VERSION 0.1.0
    DESCRIPTION "Graphics-independent C++20 game backend with a job system"
    LANGUAGES CXX)

# --------------------------------------------------------------------------
# Options
# --------------------------------------------------------------------------
option(THREADMAXX_BUILD_EXAMPLES "Build the bundled minimal example" ON)
option(THREADMAXX_BUILD_TESTS "Build and register the test suite" ON)
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
# Examples
# --------------------------------------------------------------------------
if (THREADMAXX_BUILD_EXAMPLES)
    add_subdirectory(examples/minimal)
    add_subdirectory(examples/boids)
endif()

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
if (THREADMAXX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
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
