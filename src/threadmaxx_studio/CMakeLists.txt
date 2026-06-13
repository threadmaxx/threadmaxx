set(THREADMAXX_STUDIO_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/threadmaxx_studio.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/config.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/data_source.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/direct_data_source.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panel.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/studio.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/version.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/console.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/engine_inspector.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/frame_snapshot.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/gizmo.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/hierarchy.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/menu_bar.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/profiler.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/replay.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/property_editor.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/resources.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/status_bar.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/task_graph.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/tuning.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/world_diff.hpp
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
    panels/FrameSnapshotPanel.cpp
    panels/GizmoPanel.cpp
    panels/HierarchyPanel.cpp
    panels/MenuBar.cpp
    panels/ProfilerPanel.cpp
    panels/PropertyEditorPanel.cpp
    panels/ReplayPanel.cpp
    panels/ResourcesPanel.cpp
    panels/StatusBar.cpp
    panels/TaskGraphPanel.cpp
    panels/TuningPanel.cpp
    panels/WorldDiffPanel.cpp
)

add_library(threadmaxx_studio STATIC
    ${THREADMAXX_STUDIO_SOURCES}
    ${THREADMAXX_STUDIO_PUBLIC_HEADERS}
)
add_library(threadmaxx::studio ALIAS threadmaxx_studio)

target_include_directories(threadmaxx_studio
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
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

# M4 — sibling panels. Each guards on the sibling's library target so
# studio still builds when a sibling is opted out of the build. The
# matching panel TUs forward-declare into the public header so a
# downstream tooling user can include the panel header without pulling
# the sibling lib's headers.
if (TARGET threadmaxx::animation)
    target_sources(threadmaxx_studio PRIVATE
        panels/AnimationPanel.cpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/animation.hpp)
    target_link_libraries(threadmaxx_studio PUBLIC threadmaxx::animation)
    target_compile_definitions(threadmaxx_studio PUBLIC
        THREADMAXX_STUDIO_HAS_ANIMATION_PANEL=1)
endif()

if (TARGET threadmaxx::audio)
    target_sources(threadmaxx_studio PRIVATE
        panels/AudioPanel.cpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/audio.hpp)
    target_link_libraries(threadmaxx_studio PUBLIC threadmaxx::audio)
    target_compile_definitions(threadmaxx_studio PUBLIC
        THREADMAXX_STUDIO_HAS_AUDIO_PANEL=1)
endif()

if (TARGET threadmaxx::input)
    target_sources(threadmaxx_studio PRIVATE
        panels/InputPanel.cpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/input.hpp)
    target_link_libraries(threadmaxx_studio PUBLIC threadmaxx::input)
    target_compile_definitions(threadmaxx_studio PUBLIC
        THREADMAXX_STUDIO_HAS_INPUT_PANEL=1)
endif()

if (TARGET threadmaxx::assets)
    target_sources(threadmaxx_studio PRIVATE
        panels/AssetsPanel.cpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/assets.hpp)
    target_link_libraries(threadmaxx_studio PUBLIC threadmaxx::assets)
    target_compile_definitions(threadmaxx_studio PUBLIC
        THREADMAXX_STUDIO_HAS_ASSETS_PANEL=1)
endif()

if (TARGET threadmaxx::ui)
    target_sources(threadmaxx_studio PRIVATE
        panels/UIInspectorPanel.cpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/ui_inspector.hpp)
    target_link_libraries(threadmaxx_studio PUBLIC threadmaxx::ui)
    target_compile_definitions(threadmaxx_studio PUBLIC
        THREADMAXX_STUDIO_HAS_UI_INSPECTOR_PANEL=1)
endif()

if (TARGET threadmaxx::navmesh)
    target_sources(threadmaxx_studio PRIVATE
        panels/NavmeshPanel.cpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/navmesh.hpp)
    target_link_libraries(threadmaxx_studio PUBLIC threadmaxx::navmesh)
    target_compile_definitions(threadmaxx_studio PUBLIC
        THREADMAXX_STUDIO_HAS_NAVMESH_PANEL=1)
endif()

if (TARGET threadmaxx::physics)
    target_sources(threadmaxx_studio PRIVATE
        panels/PhysicsPanel.cpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/physics.hpp)
    target_link_libraries(threadmaxx_studio PUBLIC threadmaxx::physics)
    target_compile_definitions(threadmaxx_studio PUBLIC
        THREADMAXX_STUDIO_HAS_PHYSICS_PANEL=1)
endif()

if (TARGET threadmaxx::reflect)
    target_sources(threadmaxx_studio PRIVATE
        panels/ReflectPanel.cpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/reflect.hpp)
    target_link_libraries(threadmaxx_studio PUBLIC threadmaxx::reflect)
    target_compile_definitions(threadmaxx_studio PUBLIC
        THREADMAXX_STUDIO_HAS_REFLECT_PANEL=1)
endif()

if (TARGET threadmaxx::migration)
    target_sources(threadmaxx_studio PRIVATE
        panels/SaveInspectorPanel.cpp
        panels/MigrationStepPanel.cpp
        panels/SchemaGraphPanel.cpp
        panels/MigrationValidatorPanel.cpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/save_inspector.hpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/migration_step.hpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/schema_graph.hpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/migration_validator.hpp)
    target_link_libraries(threadmaxx_studio PUBLIC threadmaxx::migration)
    target_compile_definitions(threadmaxx_studio PUBLIC
        THREADMAXX_STUDIO_HAS_MIGRATION_PANELS=1)
endif()

if (TARGET threadmaxx::network)
    target_sources(threadmaxx_studio PRIVATE
        panels/NetworkSessionPanel.cpp
        panels/SnapshotDeltaPanel.cpp
        panels/InterestPanel.cpp
        panels/DesyncPanel.cpp
        panels/PacketTracePanel.cpp
        StudioAgent.cpp
        RemoteDataSource.cpp
        ShardDirectory.cpp
        panels/BandwidthPanel.cpp
        panels/ShardPickerPanel.cpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/network_session.hpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/snapshot_delta.hpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/interest.hpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/desync.hpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/packet_trace.hpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/bandwidth.hpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/panels/shard_picker.hpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/agent.hpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/remote_data_source.hpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_studio/shard_directory.hpp)
    target_link_libraries(threadmaxx_studio PUBLIC threadmaxx::network)
    target_compile_definitions(threadmaxx_studio PUBLIC
        THREADMAXX_STUDIO_HAS_NETWORK_PANELS=1
        THREADMAXX_STUDIO_HAS_REMOTE_ATTACH=1)
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
