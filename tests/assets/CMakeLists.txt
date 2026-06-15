set(THREADMAXX_ASSETS_TESTS
    test_assets_no_engine_link
    test_assets_version
    test_assets_pod_layout
    test_assets_io_round_trip
    test_assets_obj_cube
    test_assets_obj_smoothed_normals
    test_assets_gltf
    test_assets_ply_binary
    test_assets_inflate_unit
    test_assets_bmp
    test_assets_tga
    test_assets_png
    test_assets_wav
    test_assets_bmfont
    test_assets_registry
    test_assets_async_loader
    test_assets_bundle
    test_assets_watch
    test_assets_crowd_no_alloc
    test_assets_list_resident
)

# Engine bridge test only when threadmaxx::threadmaxx target is around AND
# the bridge define is exported.
get_target_property(_assets_compile_defs threadmaxx::assets INTERFACE_COMPILE_DEFINITIONS)
if (_assets_compile_defs MATCHES "THREADMAXX_ASSETS_HAS_ENGINE_BRIDGE=1")
    add_executable(test_assets_engine_bridge test_assets_engine_bridge.cpp)
    target_link_libraries(test_assets_engine_bridge PRIVATE
        threadmaxx::assets threadmaxx::threadmaxx)
    target_include_directories(test_assets_engine_bridge PRIVATE
        ${CMAKE_SOURCE_DIR}/tests)
    target_compile_definitions(test_assets_engine_bridge PRIVATE
        THREADMAXX_ASSETS_FIXTURES_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
    set_target_properties(test_assets_engine_bridge PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(test_assets_engine_bridge PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME assets.test_assets_engine_bridge
             COMMAND test_assets_engine_bridge)
endif()

foreach(test ${THREADMAXX_ASSETS_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::assets)
    target_include_directories(${test} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
    target_compile_definitions(${test} PRIVATE
        THREADMAXX_ASSETS_FIXTURES_DIR=${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
    set_target_properties(${test} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(${test} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME assets.${test} COMMAND ${test})
endforeach()
