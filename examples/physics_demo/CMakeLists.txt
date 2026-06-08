# physics_demo — end-to-end scene exercising the threadmaxx_physics
# sibling library: character controller, static + dynamic geometry,
# and contact-event callbacks. Always built when
# `threadmaxx::physics` is available; prefers the JoltBackend at
# runtime if it was wired in, falls back to StubBackend otherwise so
# the demo runs in any configuration.

if (NOT TARGET threadmaxx::physics)
    return()
endif()

add_executable(threadmaxx_physics_demo main.cpp)

target_link_libraries(threadmaxx_physics_demo PRIVATE threadmaxx::physics)

set_target_properties(threadmaxx_physics_demo PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if (NOT MSVC)
    target_compile_options(threadmaxx_physics_demo PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
