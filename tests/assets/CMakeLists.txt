set(THREADMAXX_ASSETS_TESTS
    test_assets_no_engine_link
    test_assets_version
    test_assets_pod_layout
    test_assets_io_round_trip
    test_assets_obj_cube
    test_assets_obj_smoothed_normals
    test_assets_ply_binary
    test_assets_inflate_unit
    test_assets_bmp
    test_assets_tga
    test_assets_png
    test_assets_wav
)

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
