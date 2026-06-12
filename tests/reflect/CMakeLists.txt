# Engine-bridge tests only when the bridge define is exported. Same
# pattern as threadmaxx_assets.
get_target_property(_reflect_compile_defs threadmaxx::reflect INTERFACE_COMPILE_DEFINITIONS)
if (_reflect_compile_defs MATCHES "THREADMAXX_REFLECT_HAS_ENGINE_BRIDGE=1")
    foreach(test
            test_reflect_engine_bridge_builtins
            test_reflect_engine_bridge_user_component
            test_reflect_engine_bridge_idempotent)
        add_executable(${test} ${test}.cpp)
        target_link_libraries(${test} PRIVATE
            threadmaxx::reflect threadmaxx::threadmaxx)
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
        add_test(NAME reflect.${test} COMMAND ${test})
    endforeach()
endif()

set(THREADMAXX_REFLECT_TESTS
    test_reflect_no_engine_link
    test_reflect_version
    test_reflect_aggregate_count
    test_reflect_aggregate_for_each
    test_reflect_macro_basic
    test_reflect_macro_named_for_each
    test_reflect_macro_offsets
    test_reflect_macro_in_namespace
    test_reflect_registry_idempotent
    test_reflect_registry_lookup_by_name
    test_reflect_registry_lookup_by_type_index
    test_reflect_registry_concurrent
    test_reflect_enum_basic
    test_reflect_enum_round_trip
    test_reflect_enum_custom_range
    test_reflect_enum_flags
    test_reflect_attr_range
    test_reflect_attr_tooltip
    test_reflect_attr_multiple
    test_reflect_attr_query
    test_reflect_visit_aggregate
    test_reflect_visit_macro
    test_reflect_json_round_trip
    test_reflect_hash_visitor
    test_reflect_equals_visitor
    test_reflect_value_sbo
    test_reflect_patch_basic
    test_reflect_patch_unknown_field
    test_reflect_read_field_round_trip
)

foreach(test ${THREADMAXX_REFLECT_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::reflect)
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
    add_test(NAME reflect.${test} COMMAND ${test})
endforeach()
