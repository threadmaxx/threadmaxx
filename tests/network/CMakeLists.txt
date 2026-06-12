set(THREADMAXX_NETWORK_TESTS
    test_network_version
    test_network_bitstream_roundtrip
    test_network_bitstream_varuint
    test_network_bitstream_overflow
    test_network_bitstream_align
    test_network_loopback_send_receive
    test_network_loopback_loss
    test_network_loopback_reorder
    test_network_loopback_determinism
    test_network_handshake
    test_network_session_unique
    test_network_sequence_wrap
    test_network_input_delivery
    test_network_input_ack
    test_network_input_under_loss
    test_network_snapshot_roundtrip
    test_network_snapshot_oversize
    test_network_snapshot_schema
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
