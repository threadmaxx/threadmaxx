set(THREADMAXX_STUDIO_PUBLIC_HEADERS
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_studio/threadmaxx_studio.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_studio/config.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_studio/data_source.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_studio/direct_data_source.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_studio/panel.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_studio/studio.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_studio/version.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_studio/panels/console.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_studio/panels/engine_inspector.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_studio/panels/menu_bar.hpp
    ${CMAKE_SOURCE_DIR}/include/threadmaxx_studio/panels/status_bar.hpp
)

# ST4 closes M1 with DirectDataSource. The studio is engine-AGNOSTIC
# at the interface layer (panel.hpp / data_source.hpp stay clean);
# DirectDataSource is the first TU that pulls threadmaxx/ headers.
# The core engine lib is already on the public surface transitively
# via threadmaxx::editor → threadmaxx::threadmaxx, so no extra link
# is needed here.
set(THREADMAXX_STUDIO_SOURCES
    DirectDataSource.cpp
    PanelHost.cpp
    StudioApp.cpp
    StudioVersion.cpp
    panels/ConsolePanel.cpp
    panels/EntityInspectorPanel.cpp
    panels/MenuBar.cpp
    panels/StatusBar.cpp
)

add_library(threadmaxx_studio STATIC
    ${THREADMAXX_STUDIO_SOURCES}
    ${THREADMAXX_STUDIO_PUBLIC_HEADERS}
)
add_library(threadmaxx::studio ALIAS threadmaxx_studio)

target_include_directories(threadmaxx_studio
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(threadmaxx_studio PUBLIC cxx_std_20)

set_target_properties(threadmaxx_studio PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# The studio is engine-AGNOSTIC. The core threadmaxx target is NOT
# linked at the library level — concrete data sources (DirectDataSource
# in ST4) opt in via their own TU. This is what the no-engine-link
# canary pins.
#
# The editor v1.0 surface IS a public dependency: every panel renders
# through `editor::IEditorBackend` (ST2+) and submits mutations
# through `editor::CommandStack` (ST4+). We forward-declare
# `IEditorBackend` in `panel.hpp` to keep ST1 header-include-cheap;
# ST2 enables the editor link.
if (TARGET threadmaxx::editor)
    target_link_libraries(threadmaxx_studio PUBLIC threadmaxx::editor)
endif()

if (MSVC)
    target_compile_options(threadmaxx_studio PRIVATE /W4 /permissive-)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_studio PRIVATE /WX)
    endif()
else()
    target_compile_options(threadmaxx_studio PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wsign-conversion -Wconversion -Wold-style-cast)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_studio PRIVATE -Werror)
    endif()
endif()
