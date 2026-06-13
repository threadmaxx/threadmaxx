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
)

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
