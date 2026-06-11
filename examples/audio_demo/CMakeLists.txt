# AU7 — minimal end-to-end audio example. Demonstrates the per-tick
# listener + emitter update pattern + a `mix()` call. Headless: writes
# its output through a LoopbackDevice and prints the mixer's peak meter
# so the integration can be verified without a real sound card.
#
# Opt-in via `THREADMAXX_BUILD_EXAMPLES`; additionally guarded on the
# audio sibling library being available.

if (NOT TARGET threadmaxx::audio)
    return()
endif()

add_executable(threadmaxx_audio_demo main.cpp)

target_link_libraries(threadmaxx_audio_demo PRIVATE threadmaxx::audio)

set_target_properties(threadmaxx_audio_demo PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

if (NOT MSVC)
    target_compile_options(threadmaxx_audio_demo PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_audio_demo PRIVATE -Werror)
    endif()
endif()
