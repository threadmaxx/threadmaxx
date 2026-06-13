set(THREADMAXX_INPUT_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/threadmaxx_input.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/action.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/backend.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/binding.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/config.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/context.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/cursor.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/events.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/picking.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/state.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/trace.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/types.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/version.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/backends/GlfwBackend.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/backends/NullBackend.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/detail/deadzone.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/detail/edge_buffer.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/detail/keymap.hpp
)

set(THREADMAXX_INPUT_SOURCES
    InputContext.cpp
    Binding.cpp
    Picking.cpp
    Trace.cpp
    backends/GlfwBackend.cpp
    backends/NullBackend.cpp
)

add_library(threadmaxx_input STATIC
    ${THREADMAXX_INPUT_SOURCES}
    ${THREADMAXX_INPUT_PUBLIC_HEADERS}
)
add_library(threadmaxx::input ALIAS threadmaxx_input)

target_include_directories(threadmaxx_input
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
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

# Opt-in UI bridge: compiles UiBridge.cpp + ui_bridge.hpp when
# threadmaxx::ui is available. Exports THREADMAXX_INPUT_HAS_UI_BRIDGE=1
# so consumers can `#if`-gate.
if (TARGET threadmaxx::ui)
    target_sources(threadmaxx_input PRIVATE UiBridge.cpp)
    target_sources(threadmaxx_input PRIVATE
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_input/ui_bridge.hpp)
    target_link_libraries(threadmaxx_input PUBLIC threadmaxx::ui)
    target_compile_definitions(threadmaxx_input PUBLIC THREADMAXX_INPUT_HAS_UI_BRIDGE=1)
endif()
