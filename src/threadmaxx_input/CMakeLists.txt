set(THREADMAXX_INPUT_PUBLIC_HEADERS
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_input/threadmaxx_input.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_input/action.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_input/backend.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_input/binding.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_input/config.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_input/context.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_input/events.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_input/state.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_input/types.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_input/version.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_input/backends/NullBackend.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_input/detail/deadzone.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_input/detail/edge_buffer.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_input/detail/keymap.hpp
)

set(THREADMAXX_INPUT_SOURCES
    InputContext.cpp
    Binding.cpp
    backends/NullBackend.cpp
)

add_library(threadmaxx_input STATIC
    ${THREADMAXX_INPUT_SOURCES}
    ${THREADMAXX_INPUT_PUBLIC_HEADERS}
)
add_library(threadmaxx::input ALIAS threadmaxx_input)

target_include_directories(threadmaxx_input
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(threadmaxx_input PUBLIC cxx_std_20)

set_target_properties(threadmaxx_input PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if (MSVC)
    target_compile_options(threadmaxx_input PRIVATE /W4 /permissive-)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_input PRIVATE /WX)
    endif()
else()
    target_compile_options(threadmaxx_input PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wsign-conversion -Wconversion -Wold-style-cast)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_input PRIVATE -Werror)
    endif()
endif()
