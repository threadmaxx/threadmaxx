# threadmaxx_audio test suite (AU1+).
#
# Each test links `threadmaxx::audio` and uses the tiny `Check.hpp` harness
# from the parent `tests/` directory. Registered with CTest under the
# `audio.*` name prefix.

set(THREADMAXX_AUDIO_TESTS
    test_audio_format
    test_audio_buffer
    test_audio_loopback_device
    test_audio_mixer_one_shot
    test_audio_mixer_bus_routing
    test_audio_mixer_solo
    test_audio_mixer_voice_stealing
    test_audio_mixer_no_allocations
)

foreach(test ${THREADMAXX_AUDIO_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::audio)
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
    add_test(NAME audio.${test} COMMAND ${test})
endforeach()
