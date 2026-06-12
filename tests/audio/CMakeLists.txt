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
    test_audio_stream_basic
    test_audio_stream_rewind
    test_audio_stream_underrun
    test_audio_stream_loop
    test_audio_spatial_attenuation
    test_audio_spatial_pan
    test_audio_spatial_doppler
    test_audio_spatial_multiple_listeners
    test_audio_dsp_gain
    test_audio_dsp_pan
    test_audio_dsp_fade
    test_audio_dsp_no_allocations
    test_audio_diagnostics_meters
    test_audio_diagnostics_reset
    test_audio_diagnostics_buses
    test_audio_events_playback
    test_audio_backend_alsa
    test_audio_backend_pulse
    test_audio_voice_pool_256_no_alloc
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

# AU7 — engine integration smoke test. Links against both the audio
# sibling library and the threadmaxx core engine; only built when the
# core library target is available (same opt-in as the other audio
# tests).
if (TARGET threadmaxx::threadmaxx)
    add_executable(test_audio_engine_integration
        test_audio_engine_integration.cpp)
    target_link_libraries(test_audio_engine_integration PRIVATE
        threadmaxx::audio
        threadmaxx::threadmaxx)
    target_include_directories(test_audio_engine_integration PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_target_properties(test_audio_engine_integration PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(test_audio_engine_integration PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME audio.test_audio_engine_integration
             COMMAND test_audio_engine_integration)
endif()
