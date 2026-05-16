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
