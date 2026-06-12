set(THREADMAXX_REFLECT_TESTS
    test_reflect_no_engine_link
    test_reflect_version
    test_reflect_aggregate_count
    test_reflect_aggregate_for_each
    test_reflect_macro_basic
    test_reflect_macro_named_for_each
    test_reflect_macro_offsets
    test_reflect_macro_in_namespace
)

foreach(test ${THREADMAXX_REFLECT_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::reflect)
    target_include_directories(${test} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
    set_target_properties(${test} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(${test} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME reflect.${test} COMMAND ${test})
endforeach()
