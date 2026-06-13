set(THREADMAXX_ANIMATION_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/threadmaxx_animation.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/types.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/pose.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/skeleton.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/clip.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/registry.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/graph.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/blend.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/ik.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/warp.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/eval.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/diagnostics.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/retarget.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/serialization.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/cloth.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/detail/pose_math.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/detail/curve_eval.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/detail/graph_eval.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_animation/detail/job_batch.hpp
)

set(THREADMAXX_ANIMATION_SOURCES
    Registry.cpp
    Animator.cpp
    GraphEval.cpp
    IK.cpp
    Warp.cpp
    Batch.cpp
    Retarget.cpp
)

add_library(threadmaxx_animation STATIC
    ${THREADMAXX_ANIMATION_SOURCES}
    ${THREADMAXX_ANIMATION_PUBLIC_HEADERS}
)
add_library(threadmaxx::animation ALIAS threadmaxx_animation)

target_include_directories(threadmaxx_animation
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
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
