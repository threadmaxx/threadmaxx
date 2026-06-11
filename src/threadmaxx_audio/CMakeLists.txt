set(THREADMAXX_AUDIO_PUBLIC_HEADERS
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_audio/threadmaxx_audio.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_audio/buffer.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_audio/clip.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_audio/config.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_audio/device.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_audio/diagnostics.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_audio/dsp.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_audio/events.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_audio/loopback_device.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_audio/mixer.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_audio/spatial.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_audio/stream.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_audio/types.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_audio/voice.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_audio/detail/pan_law.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_audio/detail/voice_allocator.hpp
)

set(THREADMAXX_AUDIO_SOURCES
    AudioMixer.cpp
    VoiceAllocator.cpp
    backends/LoopbackDevice.cpp
)

add_library(threadmaxx_audio STATIC
    ${THREADMAXX_AUDIO_SOURCES}
    ${THREADMAXX_AUDIO_PUBLIC_HEADERS}
)
add_library(threadmaxx::audio ALIAS threadmaxx_audio)

target_include_directories(threadmaxx_audio
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(threadmaxx_audio PUBLIC cxx_std_20)

set_target_properties(threadmaxx_audio PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if (MSVC)
    target_compile_options(threadmaxx_audio PRIVATE /W4 /permissive-)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_audio PRIVATE /WX)
    endif()
else()
    target_compile_options(threadmaxx_audio PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wsign-conversion -Wconversion -Wold-style-cast)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_audio PRIVATE -Werror)
    endif()
endif()
