set(THREADMAXX_EDITOR_PUBLIC_HEADERS
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/threadmaxx_editor.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/backend.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/session.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/types.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/version.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/backends/headless.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/backends/remote.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/inspect.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/commands.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/hotreload.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/telemetry.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/selection.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/properties.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/gizmo.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/diff.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/layout.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/console.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/hierarchy.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/profiler.hpp
)

set(THREADMAXX_EDITOR_SOURCES
    CommandStack.cpp
    Console.cpp
    EditorSession.cpp
    Gizmo.cpp
    Hierarchy.cpp
    HotReloadController.cpp
    Inspector.cpp
    Layout.cpp
    Profiler.cpp
    PropertyEditor.cpp
    SelectionState.cpp
    TelemetryOverlay.cpp
    WorldDiff.cpp
    backends/HeadlessBackend.cpp
    backends/RemoteBackend.cpp
)

add_library(threadmaxx_editor STATIC
    ${THREADMAXX_EDITOR_SOURCES}
    ${THREADMAXX_EDITOR_PUBLIC_HEADERS}
)
add_library(threadmaxx::editor ALIAS threadmaxx_editor)

target_include_directories(threadmaxx_editor
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(threadmaxx_editor PUBLIC cxx_std_20)

set_target_properties(threadmaxx_editor PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# The editor binds against the engine's public API. We link the core
# threadmaxx as PUBLIC so consumers get the include path automatically.
target_link_libraries(threadmaxx_editor PUBLIC threadmaxx::threadmaxx)

if (MSVC)
    target_compile_options(threadmaxx_editor PRIVATE /W4 /permissive-)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_editor PRIVATE /WX)
    endif()
else()
    target_compile_options(threadmaxx_editor PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wsign-conversion -Wconversion -Wold-style-cast)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_editor PRIVATE -Werror)
    endif()
endif()

# Optional reflect binding (lit up at E7). Header inclusion is gated by
# `THREADMAXX_EDITOR_HAS_REFLECT=1`; the editor stays usable without it.
if (TARGET threadmaxx::reflect)
    target_link_libraries(threadmaxx_editor PUBLIC threadmaxx::reflect)
    target_compile_definitions(threadmaxx_editor PUBLIC
        THREADMAXX_EDITOR_HAS_REFLECT=1)
endif()

# E11 — optional Dear ImGui backend. The base library always compiles
# without ImGui; this just adds the ImGuiBackend implementation +
# public header when ImGui is in the build.
if (TARGET imgui)
    target_sources(threadmaxx_editor PRIVATE
        backends/ImGuiBackend.cpp
        ${CMAKE_SOURCE_DIR}/include/threadmaxx_editor/backends/imgui.hpp)
    target_link_libraries(threadmaxx_editor PUBLIC imgui)
    target_compile_definitions(threadmaxx_editor PUBLIC
        THREADMAXX_EDITOR_HAS_IMGUI_BACKEND=1)
endif()
