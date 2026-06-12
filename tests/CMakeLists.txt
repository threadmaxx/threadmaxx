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
    submit_interpolated_frame_test
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
    debug_camera_mask_test
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

# threadmaxx_animation sibling-library test suite (A1+). Lives in
# `tests/animation/`. Skipped if the animation opt-in is off.
if (TARGET threadmaxx::animation)
    add_subdirectory(animation)
endif()

# threadmaxx_physics sibling-library test suite (P1+). Lives in
# `tests/physics/`. Skipped if the physics opt-in is off.
if (TARGET threadmaxx::physics)
    add_subdirectory(physics)
endif()

# threadmaxx_navmesh sibling-library test suite (N1+). Lives in
# `tests/navmesh/`. Skipped if the navmesh opt-in is off.
if (TARGET threadmaxx::navmesh)
    add_subdirectory(navmesh)
endif()

# threadmaxx_audio sibling-library test suite (AU1+). Lives in
# `tests/audio/`. Skipped if the audio opt-in is off.
if (TARGET threadmaxx::audio)
    add_subdirectory(audio)
endif()

# threadmaxx_ui sibling-library test suite (UI1+). Lives in
# `tests/ui/`. Skipped if the UI opt-in is off.
if (TARGET threadmaxx::ui)
    add_subdirectory(ui)
endif()

# threadmaxx_input sibling-library test suite (I1+). Lives in
# `tests/input/`. Skipped if the input opt-in is off.
if (TARGET threadmaxx::input)
    add_subdirectory(input)
endif()

# threadmaxx_assets sibling-library test suite (A1+). Lives in
# `tests/assets/`. Skipped if the assets opt-in is off.
if (TARGET threadmaxx::assets)
    add_subdirectory(assets)
endif()

# threadmaxx_reflect sibling-library test suite (R1+). Lives in
# `tests/reflect/`. Skipped if the reflect opt-in is off.
if (TARGET threadmaxx::reflect)
    add_subdirectory(reflect)
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

    # M6.0b — CPU compositor + dirty-rect bookkeeping. End-to-end:
    # everything tested here gets uploaded into the GPU texture
    # sampled by the overlay quad.
    add_executable(tou2d_ui_overlay_bitmap_test
        tou2d_ui_overlay_bitmap_test.cpp)
    target_link_libraries(tou2d_ui_overlay_bitmap_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_ui_overlay_bitmap_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(tou2d_ui_overlay_bitmap_test PRIVATE
        THREADMAXX_TOU2D_FONT_PATH="${CMAKE_SOURCE_DIR}/assets/ui/font.ttf")
    set_target_properties(tou2d_ui_overlay_bitmap_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_ui_overlay_bitmap_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_ui_overlay_bitmap_test
             COMMAND tou2d_ui_overlay_bitmap_test)

    # M7.3 — ParticleSystem visual-polish (thruster color aging +
    # damage smoke rate curve). Static helpers + emit/buildRenderFrame
    # round-trip. Links tou2d_core for the ParticleSystem .cpp body.
    add_executable(tou2d_particles_test tou2d_particles_test.cpp)
    target_link_libraries(tou2d_particles_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_particles_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_particles_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_particles_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_particles_test COMMAND tou2d_particles_test)

    # M7.4 — faction system (bot ally targeting + friendly-fire
    # suppression). Pins LocalPlayer / PlayerSlotSetup factionId
    # defaults + the WeaponFireSystem botShotHitsAlly helper.
    add_executable(tou2d_faction_test tou2d_faction_test.cpp)
    target_link_libraries(tou2d_faction_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_faction_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_faction_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_faction_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_faction_test COMMAND tou2d_faction_test)

    # M7.5 — pickup framework split (RepairBase tile vs RepairKit
    # entity). Pins PickupKind / Pickup / PickupSpec / Attribute byte
    # values + respawn-countdown shape.
    add_executable(tou2d_pickup_framework_test tou2d_pickup_framework_test.cpp)
    target_link_libraries(tou2d_pickup_framework_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_pickup_framework_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_pickup_framework_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_pickup_framework_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_pickup_framework_test COMMAND tou2d_pickup_framework_test)

    # M7.6 — water mechanic. Pins Attribute::Water enum byte,
    # TerrainGrid::setWater round-trip, tuning-constant bands, and the
    # MovementSystem integrate-step semantics (air-only vs fully-wet vs
    # smooth blend at half wetness).
    add_executable(tou2d_water_test tou2d_water_test.cpp)
    target_link_libraries(tou2d_water_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_water_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_water_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_water_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_water_test COMMAND tou2d_water_test)

    # M6.0b — UISystem state machine + UIScreen enum + UIScreenChanged
    # event payload pin.
    add_executable(tou2d_uiscreen_state_machine_test
        tou2d_uiscreen_state_machine_test.cpp)
    target_link_libraries(tou2d_uiscreen_state_machine_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_uiscreen_state_machine_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_uiscreen_state_machine_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_uiscreen_state_machine_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_uiscreen_state_machine_test
             COMMAND tou2d_uiscreen_state_machine_test)

    # M6.1 — MainMenu row table + focus / moveFocus / acceptFocused
    # contract. Exercises the menu state machine main.cpp drives every
    # frame; same harness as the M6.0b enum-state pin.
    add_executable(tou2d_menu_navigation_test
        tou2d_menu_navigation_test.cpp)
    target_link_libraries(tou2d_menu_navigation_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_menu_navigation_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_menu_navigation_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_menu_navigation_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_menu_navigation_test
             COMMAND tou2d_menu_navigation_test)

    # M6.2 — MatchSetup screen row table + scroller cycle + Start /
    # Back accept dispatch + MatchSetup-from-menu byte equality with
    # MatchSetup-from-CLI (determinism contract).
    add_executable(tou2d_match_setup_test
        tou2d_match_setup_test.cpp)
    target_link_libraries(tou2d_match_setup_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_match_setup_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_match_setup_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_match_setup_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_match_setup_test
             COMMAND tou2d_match_setup_test)

    # 2026-05-31 — imported-level picker contract: LevelEnumerator
    # sort+filter, UISystem Level scroller domain (empty / populated /
    # shrink-revalidate), and LevelLoader JPEG-derived fallback.
    add_executable(tou2d_level_picker_test
        tou2d_level_picker_test.cpp)
    target_link_libraries(tou2d_level_picker_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_level_picker_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_level_picker_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_level_picker_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_level_picker_test
             COMMAND tou2d_level_picker_test)

    # 2026-05-31 — .lev /KEY value blob layout. Hermetic synthetic-
    # bytes parser pin; ties the offset table in TOU_RE.md to a build-
    # time assertion so future format changes can't drift silently.
    add_executable(tou2d_lev_config_test
        tou2d_lev_config_test.cpp)
    target_include_directories(tou2d_lev_config_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_lev_config_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_lev_config_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_lev_config_test
             COMMAND tou2d_lev_config_test)

    # M6.4 — Pause screen row table, Resume/Restart/Return-to-main/
    # Quit accept dispatch, pending* flag stickiness + independence,
    # and MainMenu Continue-greyed posture (the placeholder for the
    # engine-restart-with-MatchSetup follow-up).
    add_executable(tou2d_pause_test
        tou2d_pause_test.cpp)
    target_link_libraries(tou2d_pause_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_pause_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_pause_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_pause_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_pause_test
             COMMAND tou2d_pause_test)

    # M6.10 — universal-Esc routing. Pins `UISystem::triggerBack()`
    # for every screen in the graph + the Options sub-screen → Options
    # → MainMenu chain (Esc-out-of-OptionsX now hits the parent Options
    # screen first AND fires `pendingSettingsSave_` on the second hop;
    # the pre-M6.10 main.cpp UiCancel shortcut silently skipped both).
    add_executable(tou2d_ui_back_routing_test
        tou2d_ui_back_routing_test.cpp)
    target_link_libraries(tou2d_ui_back_routing_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_ui_back_routing_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_ui_back_routing_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_ui_back_routing_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_ui_back_routing_test
             COMMAND tou2d_ui_back_routing_test)

    # M6.3 — PlayerSetup screen row table, per-slot scroller cycles
    # (tag alphabet, role tristate, ship kind, palette), per-slot write
    # isolation, formatRow rendering, BackToMain → MatchSetup routing,
    # and the default-MatchSetup byte-equality contract underwriting
    # "unedited menu run == CLI run".
    add_executable(tou2d_player_setup_test
        tou2d_player_setup_test.cpp)
    target_link_libraries(tou2d_player_setup_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_player_setup_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_player_setup_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_player_setup_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_player_setup_test
             COMMAND tou2d_player_setup_test)

    # M6.6 — Results screen contract: showResults transitions to Results
    # with the snapshot captured; Display rows render winner banner +
    # per-slot scoreboard; moveFocus skips Display rows so initial focus
    # lands on the first action button; Rematch/ReturnToSetup/MainMenu
    # action routing; pendingRematch sticky-flag flow.
    add_executable(tou2d_results_screen_test
        tou2d_results_screen_test.cpp)
    target_link_libraries(tou2d_results_screen_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_results_screen_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_results_screen_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_results_screen_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_results_screen_test
             COMMAND tou2d_results_screen_test)

    # tou2d_toast_render_test — M6.8 contract pin. UIToast POD layout
    # (32 B); per-slot LIFO stack push / cap enforcement; broadcast
    # sentinel fans to all four slots; ageing in preStep decrements
    # remainingTicks and drops on zero; duration==0 is a no-op push;
    # activeForSlot() returns the empty span for out-of-range slots.
    add_executable(tou2d_toast_render_test
        tou2d_toast_render_test.cpp)
    target_link_libraries(tou2d_toast_render_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_toast_render_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_toast_render_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_toast_render_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_toast_render_test
             COMMAND tou2d_toast_render_test)

    # tou2d_settings_io_test — M6.5 contract pin. POD layout (Settings
    # = 268 B); round-trip save/load preserves every field including
    # KeyMap rows + inline jsonl path; missing/short/bad-magic/
    # bad-version files all fail cleanly without mutating `out`;
    # atomic write produces the final file (tmp companion gone);
    # defaultSettingsPath honours XDG_CONFIG_HOME / HOME fallback.
    add_executable(tou2d_settings_io_test
        tou2d_settings_io_test.cpp)
    target_link_libraries(tou2d_settings_io_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_settings_io_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_settings_io_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_settings_io_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_settings_io_test
             COMMAND tou2d_settings_io_test)

    # tou2d_options_screen_test — M6.5 contract pin. Options sub-screen
    # navigation (Options → 6 sub-screens → Back → Options → Back +
    # save → MainMenu); per-knob cycling mutates settings_ as expected
    # and the Audio knobs emit AudioVolumeChanged; benchmark presets
    # pre-fill matchSetup_ and fire pendingStartMatch_; Resolution
    # Display row renders dynamic content.
    add_executable(tou2d_options_screen_test
        tou2d_options_screen_test.cpp)
    target_link_libraries(tou2d_options_screen_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_options_screen_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_options_screen_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_options_screen_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_options_screen_test
             COMMAND tou2d_options_screen_test)

    # tou2d_hud_accessibility_test — M6.7 contract pin. HudSystem's
    # `setAccessibility(AccessibilitySettings)` plumbing:
    #   * hudScale rescales HP-bar length end-to-end (50/100/150%);
    #   * low-HP red pulse fires at hpFrac == kLowHpFracThreshold and
    #     below (replaces the stacked-line fill color);
    #   * top-of-viewport warning marker fires at the same threshold
    #     and doubles in size when bigWarnings == 1;
    #   * bar is drawn as 3 stacked lines + outline pair (5 lines for
    #     the background track + outline; +3 for the fill when alive).
    add_executable(tou2d_hud_accessibility_test
        tou2d_hud_accessibility_test.cpp)
    target_link_libraries(tou2d_hud_accessibility_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_hud_accessibility_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_hud_accessibility_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_hud_accessibility_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_hud_accessibility_test
             COMMAND tou2d_hud_accessibility_test)

    # tou2d_particle_photosensitive_test — M6.7 contract pin.
    # `ParticleSystem::setAccessibility` flips a render-only alpha cap
    # (= 0.4×) on `buildRenderFrame`'s emitted DebugPoint alphas; the
    # internal pool is unchanged (commitHash-safe). Test emits a
    # death-explosion burst, then renders twice (cap off / on) and
    # verifies the alpha histogram is uniformly multiplied.
    add_executable(tou2d_particle_photosensitive_test
        tou2d_particle_photosensitive_test.cpp)
    target_link_libraries(tou2d_particle_photosensitive_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_particle_photosensitive_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_particle_photosensitive_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_particle_photosensitive_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_particle_photosensitive_test
             COMMAND tou2d_particle_photosensitive_test)

    # tou2d_debug_overlay_test — M6.9 contract pin. DebugOverlaySystem
    # is F3-toggled; off by default; while off `buildRenderFrame` emits
    # zero primitives. While on it reads `Engine::frameSnapshot` (or a
    # test-injected snapshot) and emits a vertical strip of debug-text
    # rows: FPS, tick/commitHash/workers, entity/cmd/job counts, commit
    # + render-build breakdown, slot count, and top-3 systems by
    # avgUpdateSeconds. Test pushes synthesized stats and verifies the
    # row substrings + the top-N sort.
    add_executable(tou2d_debug_overlay_test
        tou2d_debug_overlay_test.cpp)
    target_link_libraries(tou2d_debug_overlay_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_debug_overlay_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_debug_overlay_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_debug_overlay_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_debug_overlay_test
             COMMAND tou2d_debug_overlay_test)

    # tou2d_bot_behavior_test — M7.1 contract pin. Exercises the
    # `findNearestRepairTile` helper that BotControlSystem uses to
    # decide low-HP retreat vs. engage (M7.1.a) and the
    # `kBotChaosFireChancePerTick` constant the chaos-fire roll
    # consumes (M7.1.b). Pure-function surface; no Engine stand-up.
    add_executable(tou2d_bot_behavior_test
        tou2d_bot_behavior_test.cpp)
    target_link_libraries(tou2d_bot_behavior_test PRIVATE
        threadmaxx::threadmaxx tou2d_core)
    target_include_directories(tou2d_bot_behavior_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(tou2d_bot_behavior_test PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    if (NOT MSVC)
        target_compile_options(tou2d_bot_behavior_test PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow)
    endif()
    add_test(NAME tou2d_bot_behavior_test
             COMMAND tou2d_bot_behavior_test)
endif()
