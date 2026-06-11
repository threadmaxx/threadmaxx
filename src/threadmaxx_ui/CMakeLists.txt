set(THREADMAXX_UI_PUBLIC_HEADERS
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_ui/threadmaxx_ui.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_ui/backend.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_ui/config.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_ui/context.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_ui/draw.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_ui/input.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_ui/layout.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_ui/types.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_ui/version.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_ui/widget.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_ui/backends/NullBackend.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_ui/detail/clip_stack.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_ui/detail/id_stack.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_ui/detail/rect_math.hpp
)

set(THREADMAXX_UI_SOURCES
    Input.cpp
    Layout.cpp
    Widgets.cpp
    backends/NullBackend.cpp
)

add_library(threadmaxx_ui STATIC
    ${THREADMAXX_UI_SOURCES}
    ${THREADMAXX_UI_PUBLIC_HEADERS}
)
add_library(threadmaxx::ui ALIAS threadmaxx_ui)

target_include_directories(threadmaxx_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(threadmaxx_ui PUBLIC cxx_std_20)

set_target_properties(threadmaxx_ui PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if (MSVC)
    target_compile_options(threadmaxx_ui PRIVATE /W4 /permissive-)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_ui PRIVATE /WX)
    endif()
else()
    target_compile_options(threadmaxx_ui PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wsign-conversion -Wconversion -Wold-style-cast)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_ui PRIVATE -Werror)
    endif()
endif()
