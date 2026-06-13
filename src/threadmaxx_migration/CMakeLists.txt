set(THREADMAXX_MIGRATION_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_migration/threadmaxx_migration.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_migration/io.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_migration/records.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_migration/component.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_migration/pipeline.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_migration/registry.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_migration/rename.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_migration/report.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_migration/savefile.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_migration/transform.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_migration/validation.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_migration/version.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_migration/detail/binary_reader.hpp
    ${PROJECT_SOURCE_DIR}/include/threadmaxx_migration/detail/binary_writer.hpp
)

set(THREADMAXX_MIGRATION_SOURCES
    RecordIo.cpp
    MigrationRegistry.cpp
    FieldHelpers.cpp
    MigrationPipeline.cpp
    ComponentMigrationBridge.cpp
    Validation.cpp
)

add_library(threadmaxx_migration STATIC
    ${THREADMAXX_MIGRATION_SOURCES}
    ${THREADMAXX_MIGRATION_PUBLIC_HEADERS}
)
add_library(threadmaxx::migration ALIAS threadmaxx_migration)

target_include_directories(threadmaxx_migration
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(threadmaxx_migration PUBLIC cxx_std_20)

set_target_properties(threadmaxx_migration PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

if (MSVC)
    target_compile_options(threadmaxx_migration PRIVATE /W4 /permissive-)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_migration PRIVATE /WX)
    endif()
else()
    target_compile_options(threadmaxx_migration PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wsign-conversion -Wconversion -Wold-style-cast)
    if (THREADMAXX_WARNINGS_AS_ERRORS)
        target_compile_options(threadmaxx_migration PRIVATE -Werror)
    endif()
endif()

# M6 — opt-in engine bridge. Compiles the WorldSnapshot adapter when
# threadmaxx::threadmaxx is available. Exports
# THREADMAXX_MIGRATION_HAS_ENGINE_BRIDGE=1 so consumers + tests can
# `#if`-gate on it.
if (TARGET threadmaxx::threadmaxx)
    target_sources(threadmaxx_migration PRIVATE
        WorldSnapshotMigrator.cpp
        ${PROJECT_SOURCE_DIR}/include/threadmaxx_migration/world.hpp)
    target_link_libraries(threadmaxx_migration PUBLIC threadmaxx::threadmaxx)
    target_compile_definitions(threadmaxx_migration PUBLIC
        THREADMAXX_MIGRATION_HAS_ENGINE_BRIDGE=1)
endif()
