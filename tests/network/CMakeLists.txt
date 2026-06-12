set(THREADMAXX_NETWORK_TESTS
    test_network_version
    test_network_bitstream_roundtrip
    test_network_bitstream_varuint
    test_network_bitstream_overflow
    test_network_bitstream_align
)

foreach(test ${THREADMAXX_NETWORK_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::network)
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
    add_test(NAME network.${test} COMMAND ${test})
endforeach()
