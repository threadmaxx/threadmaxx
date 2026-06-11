# threadmaxx_input test suite (I1+).
#
# Each test links `threadmaxx::input` and uses the tiny `Check.hpp` harness
# from the parent `tests/` directory. Registered with CTest under the
# `input.*` name prefix.

set(THREADMAXX_INPUT_TESTS
    test_input_state_pod
    test_input_event_variant
    test_input_context_lifecycle
    test_input_no_allocations
    test_input_no_engine_link
)

foreach(test ${THREADMAXX_INPUT_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::input)
    target_include_directories(${test} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}
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
    add_test(NAME input.${test} COMMAND ${test})
endforeach()
