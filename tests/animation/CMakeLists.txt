# threadmaxx_animation test suite (A1+).
#
# Each test links `threadmaxx::animation` (which transitively pulls in
# `threadmaxx::threadmaxx`) and uses the tiny `Check.hpp` harness from
# the parent `tests/` directory. Registered with CTest under the
# `animation.*` name prefix.

set(THREADMAXX_ANIMATION_TESTS
    test_animation_skeleton
    test_animation_clip
    test_animation_pose_buffer
    test_animation_pose_math
    test_animation_clip_sampling
    test_animation_event_tracks
    test_animation_graph_basic
    test_animation_graph_param
    test_animation_graph_dirty
)

foreach(test ${THREADMAXX_ANIMATION_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::animation)
    target_include_directories(${test} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
    set_target_properties(${test} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(${test} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME animation.${test} COMMAND ${test})
endforeach()
