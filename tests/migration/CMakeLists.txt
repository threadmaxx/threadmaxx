set(THREADMAXX_MIGRATION_TESTS
    test_migration_version_compare
    test_migration_record_roundtrip
    test_migration_recordset_metadata
    test_migration_corrupted_blob
    test_migration_metadata_commit_hash
    test_migration_metadata_build_id
    test_migration_metadata_reject_unknown_format
    test_migration_registry_register
    test_migration_registry_alias
    test_migration_registry_hasPath
    test_migration_rename
    test_migration_remap_widen
    test_migration_remap_default_on_missing
    test_migration_remap_split
    test_migration_pipeline_one_step
    test_migration_pipeline_multi_step
    test_migration_pipeline_no_path
    test_migration_pipeline_unknown_type
    test_migration_pipeline_warnings
    test_migration_pipeline_stable_ids
    test_migration_codec_register
    test_migration_codec_versioned
    test_migration_codec_missing
    test_migration_validation_unknown_types
    test_migration_validation_orphaned_aliases
    test_migration_report_summary
)

get_target_property(_migration_compile_defs threadmaxx::migration
    INTERFACE_COMPILE_DEFINITIONS)
if (_migration_compile_defs MATCHES "THREADMAXX_MIGRATION_HAS_ENGINE_BRIDGE=1")
    list(APPEND THREADMAXX_MIGRATION_TESTS
        test_migration_snapshot_export
        test_migration_snapshot_import_identity
        test_migration_snapshot_migrated_roundtrip
        test_migration_snapshot_stable_id_preservation)
endif()

# M8 — convert tool smoke test. Lives outside the regular loop
# because it needs the convert exe path passed in via env var.
if (TARGET threadmaxx_migration_convert)
    add_executable(test_migration_convert_tool test_migration_convert_tool.cpp)
    target_link_libraries(test_migration_convert_tool PRIVATE threadmaxx::migration)
    target_include_directories(test_migration_convert_tool PRIVATE
        ${CMAKE_SOURCE_DIR}/tests)
    set_target_properties(test_migration_convert_tool PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(test_migration_convert_tool PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME migration.test_migration_convert_tool
             COMMAND test_migration_convert_tool)
    set_tests_properties(migration.test_migration_convert_tool PROPERTIES
        ENVIRONMENT
        "THREADMAXX_MIGRATION_CONVERT_BIN=$<TARGET_FILE:threadmaxx_migration_convert>")
endif()

foreach(test ${THREADMAXX_MIGRATION_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::migration)
    target_include_directories(${test} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
    set_target_properties(${test} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(${test} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME migration.${test} COMMAND ${test})
endforeach()
