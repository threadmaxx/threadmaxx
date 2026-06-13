set(THREADMAXX_UI_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/threadmaxx_ui.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/backend.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/config.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/context.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/debug.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/dragdrop.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/draw.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/gizmo.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/input.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/inspect.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/layout.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/menu.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/panel.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/tree.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/types.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/version.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/widget.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/backends/NullBackend.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/backends/VertexBackend.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/detail/clip_stack.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/detail/id_stack.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_ui/detail/rect_math.hpp
)

set(THREADMAXX_UI_SOURCES
    Debug.cpp
    DragDrop.cpp
    Gizmo.cpp
    Input.cpp
    Inspect.cpp
    Layout.cpp
    Menu.cpp
    Panel.cpp
    Tree.cpp
    Widgets.cpp
    backends/NullBackend.cpp
    backends/VertexBackend.cpp
)

add_library(threadmaxx_ui STATIC
    ${THREADMAXX_UI_SOURCES}
    ${THREADMAXX_UI_PUBLIC_HEADERS}
)
add_library(threadmaxx::ui ALIAS threadmaxx_ui)

target_include_directories(threadmaxx_ui
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
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
