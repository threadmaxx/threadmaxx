# Boids example — depends on SDL2. If SDL2 isn't available on the system the
# example is silently skipped so the main build still succeeds.

find_package(SDL2 QUIET)

if (NOT SDL2_FOUND)
    message(STATUS "SDL2 not found — skipping boids example (install libsdl2-dev / sdl2)")
    return()
endif()

add_executable(threadmaxx_boids
    main.cpp
    BoidsGame.cpp
    BoidsSystem.cpp
    MoveSystem.cpp
    SDLRenderer.cpp
)

target_link_libraries(threadmaxx_boids PRIVATE threadmaxx::threadmaxx)

# Prefer the modern imported target if the config file provides it; otherwise
# fall back to the legacy variables. Both forms ship in mainline SDL2.
if (TARGET SDL2::SDL2)
    target_link_libraries(threadmaxx_boids PRIVATE SDL2::SDL2)
else()
    target_include_directories(threadmaxx_boids PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(threadmaxx_boids PRIVATE ${SDL2_LIBRARIES})
endif()

set_target_properties(threadmaxx_boids PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
