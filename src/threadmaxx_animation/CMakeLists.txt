set(THREADMAXX_ANIMATION_PUBLIC_HEADERS
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_animation/threadmaxx_animation.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_animation/types.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_animation/pose.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_animation/skeleton.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_animation/clip.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_animation/registry.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_animation/graph.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_animation/blend.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_animation/ik.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_animation/eval.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_animation/detail/pose_math.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_animation/detail/curve_eval.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_animation/detail/graph_eval.hpp
)

set(THREADMAXX_ANIMATION_SOURCES
    Registry.cpp
    Animator.cpp
    GraphEval.cpp
    IK.cpp
)

add_library(threadmaxx_animation STATIC
    ${THREADMAXX_ANIMATION_SOURCES}
    ${THREADMAXX_ANIMATION_PUBLIC_HEADERS}
)
add_library(threadmaxx::animation ALIAS threadmaxx_animation)

target_include_directories(threadmaxx_animation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(threadmaxx_animation PUBLIC cxx_std_20)

# Reuses threadmaxx::Vec3 / Quat from the core lib — public link so a
# single `target_link_libraries(... threadmaxx::animation)` on a
# consumer pulls in both.
target_link_libraries(threadmaxx_animation PUBLIC threadmaxx::threadmaxx)

set_target_properties(threadmaxx_animation PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if (MSVC)
    target_compile_options(threadmaxx_animation PRIVATE /W4 /permissive-)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_animation PRIVATE /WX)
    endif()
else()
    target_compile_options(threadmaxx_animation PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wsign-conversion -Wconversion -Wold-style-cast)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_animation PRIVATE -Werror)
    endif()
endif()
