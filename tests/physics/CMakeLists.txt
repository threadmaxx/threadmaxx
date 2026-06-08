# threadmaxx_physics test suite (P1+).
#
# Each test links `threadmaxx::physics` (which transitively pulls in
# `threadmaxx::threadmaxx` for the math PODs) and uses the tiny
# `Check.hpp` harness from the parent `tests/` directory. Registered
# with CTest under the `physics.*` name prefix.

set(THREADMAXX_PHYSICS_TESTS
    test_physics_types_layout
    test_physics_stub_create_destroy
    test_physics_stub_step_noop
    test_physics_backend_conformance
    test_physics_shape_create
    test_physics_shape_share
    test_physics_shape_compound
)

foreach(test ${THREADMAXX_PHYSICS_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::physics)
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
    add_test(NAME physics.${test} COMMAND ${test})
endforeach()
