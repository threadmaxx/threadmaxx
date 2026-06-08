set(THREADMAXX_PHYSICS_PUBLIC_HEADERS
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_physics/threadmaxx_physics.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_physics/types.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_physics/body.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_physics/shape.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_physics/config.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_physics/backend.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_physics/step.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_physics/stub_backend.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_physics/sync.hpp
)

set(THREADMAXX_PHYSICS_SOURCES
    backends/StubBackend.cpp
)

add_library(threadmaxx_physics STATIC
    ${THREADMAXX_PHYSICS_SOURCES}
    ${THREADMAXX_PHYSICS_PUBLIC_HEADERS}
)
add_library(threadmaxx::physics ALIAS threadmaxx_physics)

target_include_directories(threadmaxx_physics
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(threadmaxx_physics PUBLIC cxx_std_20)

# Reuses threadmaxx::Vec3 / Quat from the core lib — public link so a
# single `target_link_libraries(... threadmaxx::physics)` on a
# consumer pulls in both.
target_link_libraries(threadmaxx_physics PUBLIC threadmaxx::threadmaxx)

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
