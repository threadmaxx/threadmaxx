set(THREADMAXX_ASSETS_TESTS
    test_assets_no_engine_link
    test_assets_version
    test_assets_pod_layout
    test_assets_io_round_trip
)

foreach(test ${THREADMAXX_ASSETS_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::assets)
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
    add_test(NAME assets.${test} COMMAND ${test})
endforeach()
