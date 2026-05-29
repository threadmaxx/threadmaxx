# Tiny no-dependency test harness. Each test is its own executable; CTest
# runs them and a non-zero exit means failure.

set(THREADMAXX_TESTS
    entity_storage_test
    command_buffer_test
    job_system_test
    determinism_test
    stats_test
    system_stats_test
    query_test
    alpha_test
    parallel_systems_test
    acceleration_test
    resource_registry_test
    component_mask_test
    hierarchy_test
    job_system_stress_test
    lifecycle_hooks_test
    scratch_arena_test
    event_channel_test
    pause_timescale_test
    reserved_handle_test
    small_wins_test
    world_has_get_test
    bundle_test
    parent_autoderive_test
    register_system_at_test
    reserve_handles_batch_test
    frame_snapshot_test
    resource_loader_test
    spatial_hash_test
    logger_test
    hierarchy_scale_test
    event_subscribe_test
    job_histogram_test
    system_wait_test
    chrome_trace_test
    serialization_test
    determinism_golden_test
    wide_mask_test
    new_components_test
    tags_test
    mask_cache_test
    resource_handle_test
    asset_reload_test
    preload_until_test
    loader_shutdown_test
    loader_stats_test
    subscription_raii_test
    render_frame_builder_test
    visibility_culling_test
    instance_buffer_layout_test
    upload_ring_test
    render_passes_test
    build_render_frame_timing_test
    component_transition_test
    archetype_signatures_test
    archetype_storage_stress_test
    foreach_chunk_test
    user_component_test
    task_graph_test
    cancellation_test
    commit_hash_test
    sharded_commit_test
    migration_batch_test
    command_buffer_routing_test
    pass_c_split_test
    workload_aware_commit_test
    archetype_hash_determinism_test
    v1_2_legacy_commit_hash_test
    event_channel_lockfree_test
    world_view_test
    commit_soak_test
    telemetry_sink_test
    render_integration_test
    integration_kitchen_sink_test
    hierarchy_cycle_test
    event_callback_reemit_test
    renderer_resize_test
    resource_handle_indirection_test
    debug_text_owning_test
    build_render_frame_seconds_test
    render_pass_ordering_test
    render_frame_interpolation_test
    visibility_culling_32_cam_test
    file_trace_sink_rotation_test
    stitched_view_concurrency_test
    concurrency_soak_test
    async_snapshot_test
    engine_user_component_lookup_test
    command_buffer_spawn_n_test
    world_for_each_chunk_of_test
    event_channel_cache_test
    for_each_serial_test
    version_test
    version_test_v1_2
    worker_cap_test
    sub_job_telemetry_test
    tuning_patch_application_test
    adaptive_grain_convergence_test
    adaptive_no_oscillation_test
    adaptive_determinism_test
    job_system_missed_wakeup_test
    tou2d_shp_import_test
    tou2d_pal_col_test
    tou2d_shp_body_test
    tou2d_proc_gen_test
    tou2d_specials_test
    tou2d_repair_pickup_test
    tou2d_weapon_mechanics_test
    # rpg_save_load_test — superseded by tests/rpg_demo/test_round_trip
    # (batch D-audit) which exercises the actual SaveLoadSystem rather
    # than re-implementing the wire format.
)

foreach(test ${THREADMAXX_TESTS})
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE threadmaxx::threadmaxx)
    target_include_directories(${test} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(${test} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(${test} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME ${test} COMMAND ${test})
endforeach()

# §3.8 batch 32 — 10k-tick concurrency soak. NOT registered with
# default ctest (runs ~3-5 minutes per invocation). Opt-in via
# -DTHREADMAXX_BUILD_LONG_SOAK=ON.
option(THREADMAXX_BUILD_LONG_SOAK
       "Build the 10,000-tick concurrency soak test (not registered with ctest)"
       OFF)
if (THREADMAXX_BUILD_LONG_SOAK)
    add_executable(concurrency_soak_long concurrency_soak_long.cpp)
    target_link_libraries(concurrency_soak_long PRIVATE threadmaxx::threadmaxx)
    target_include_directories(concurrency_soak_long PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(concurrency_soak_long PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(concurrency_soak_long PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    # Intentionally NOT add_test() — explicit invocation only.
endif()

# §3.11 batch D-audit — RPG demo test suite. Lives in a separate
# subfolder + links the rpg_demo_core static library so the actual
# shipped systems are exercised (rather than test-side re-implementations).
# Skipped when the rpg_demo's optional Vulkan deps are missing.
if (TARGET rpg_demo_core)
    add_subdirectory(rpg_demo)
endif()

# threadmaxx_simd sibling-library test suite (S1+). Lives in
# `tests/simd/`. Skipped if the SIMD opt-in is off.
if (TARGET threadmaxx::simd)
    add_subdirectory(simd)
endif()

# M5.7 — tests that need to link tou2d_core (CameraSystem's full
# implementation). Skipped when tou2d isn't built (no Vulkan renderer).
if (TARGET tou2d_core)
    add_executable(tou2d_camera_zoom_test tou2d_camera_zoom_test.cpp)
    target_link_libraries(tou2d_camera_zoom_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_camera_zoom_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_camera_zoom_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_camera_zoom_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_camera_zoom_test COMMAND tou2d_camera_zoom_test)

    # M6.0 — TTF font bake + text layout. Pins the FontAtlas / glyph
    # metrics / textPrintf / measureText contract against the bundled
    # `assets/ui/font.ttf`. Drop-in: swap the TTF, the test still passes.
    add_executable(tou2d_font_atlas_test tou2d_font_atlas_test.cpp)
    target_link_libraries(tou2d_font_atlas_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_font_atlas_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(tou2d_font_atlas_test PRIVATE
        THREADMAXX_TOU2D_FONT_PATH="${CMAKE_SOURCE_DIR}/assets/ui/font.ttf")
    set_target_properties(tou2d_font_atlas_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_font_atlas_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_font_atlas_test COMMAND tou2d_font_atlas_test)

    # M6.0 — KeyMap default-binding pin. Asserts the Action enum
    # ordering + per-slot key bindings haven't shifted (settings.dat
    # wire-shape lives on this). Links tou2d_core + glfw for
    # `makeDefaultKeyMap()` + GLFW key constants.
    add_executable(tou2d_keymap_default_binding_test
        tou2d_keymap_default_binding_test.cpp)
    target_link_libraries(tou2d_keymap_default_binding_test PRIVATE
        threadmaxx::threadmaxx tou2d_core glfw)
    target_include_directories(tou2d_keymap_default_binding_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_keymap_default_binding_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_keymap_default_binding_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_keymap_default_binding_test
             COMMAND tou2d_keymap_default_binding_test)
endif()
