set(THREADMAXX_AUDIO_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/threadmaxx_audio.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/buffer.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/clip.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/config.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/device.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/diagnostics.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/dsp.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/events.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/loopback_device.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/mixer.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/scene.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/spatial.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/stream.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/types.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/version.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/voice.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/detail/pan_law.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/detail/voice_allocator.hpp
)

set(THREADMAXX_AUDIO_SOURCES
    AudioMixer.cpp
    VoiceAllocator.cpp
    backends/LoopbackDevice.cpp
)

# AU8 — optional Linux backends. `find_package(ALSA)` and
# `pkg_check_modules(PULSE libpulse-simple)` are both QUIET; missing libs
# silently skip the backend so the build still succeeds on platforms
# without them. Public consumers can `#ifdef THREADMAXX_AUDIO_HAS_ALSA`
# (resp. _PULSE) to check at compile time.
set(THREADMAXX_AUDIO_HAS_ALSA OFF)
set(THREADMAXX_AUDIO_HAS_PULSE OFF)

if (UNIX AND NOT APPLE)
    find_package(ALSA QUIET)
    if (ALSA_FOUND)
        set(THREADMAXX_AUDIO_HAS_ALSA ON)
        list(APPEND THREADMAXX_AUDIO_SOURCES backends/AlsaDevice.cpp)
        list(APPEND THREADMAXX_AUDIO_PUBLIC_HEADERS
            ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/alsa_device.hpp)
    endif()

    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(PULSESIMPLE QUIET libpulse-simple)
        if (PULSESIMPLE_FOUND)
            set(THREADMAXX_AUDIO_HAS_PULSE ON)
            list(APPEND THREADMAXX_AUDIO_SOURCES backends/PulseDevice.cpp)
            list(APPEND THREADMAXX_AUDIO_PUBLIC_HEADERS
                ${PROJECT_SOURCE_DIR}/include/threadmaxx_audio/pulse_device.hpp)
        endif()
    endif()
endif()

add_library(threadmaxx_audio STATIC
    ${THREADMAXX_AUDIO_SOURCES}
    ${THREADMAXX_AUDIO_PUBLIC_HEADERS}
)
add_library(threadmaxx::audio ALIAS threadmaxx_audio)

target_include_directories(threadmaxx_audio
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(threadmaxx_audio PUBLIC cxx_std_20)

if (THREADMAXX_AUDIO_HAS_ALSA)
    target_link_libraries(threadmaxx_audio PRIVATE ALSA::ALSA)
    target_compile_definitions(threadmaxx_audio PUBLIC THREADMAXX_AUDIO_HAS_ALSA=1)
endif()

if (THREADMAXX_AUDIO_HAS_PULSE)
    target_include_directories(threadmaxx_audio PRIVATE ${PULSESIMPLE_INCLUDE_DIRS})
    target_link_libraries(threadmaxx_audio PRIVATE ${PULSESIMPLE_LIBRARIES})
    target_compile_definitions(threadmaxx_audio PUBLIC THREADMAXX_AUDIO_HAS_PULSE=1)
endif()

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
