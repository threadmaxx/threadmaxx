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

# --------------------------------------------------------------------------
# Library
# --------------------------------------------------------------------------
set(THREADMAXX_PUBLIC_HEADERS
    include/threadmaxx/threadmaxx.hpp
    include/threadmaxx/Config.hpp
    include/threadmaxx/Handles.hpp
    include/threadmaxx/Components.hpp
    include/threadmaxx/CommandBuffer.hpp
    include/threadmaxx/Query.hpp
    include/threadmaxx/RenderFrame.hpp
    include/threadmaxx/Stats.hpp
    include/threadmaxx/System.hpp
    include/threadmaxx/Renderer.hpp
    include/threadmaxx/Game.hpp
    include/threadmaxx/World.hpp
    include/threadmaxx/Engine.hpp
)

set(THREADMAXX_SOURCES
    src/EntityStorage.cpp
    src/JobSystem.cpp
    src/CommandBuffer.cpp
    src/World.cpp
    src/Engine.cpp
    src/EngineImpl.cpp
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
