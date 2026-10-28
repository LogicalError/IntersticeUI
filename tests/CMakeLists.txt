list(APPEND GUI_CORE_TEST_FILES
  gui_text_system_stub.h
  controls_basic_test.cpp
  dictionary_field_test.cpp
  skribidi_text_measurement_test.cpp
  controls_grid_view_test.cpp
  controls_menu_test.cpp
  transition_manager_test.cpp
  controls_scrollbar_test.cpp
  controls_slider_integration_test.cpp
  controls_tabs_test.cpp
  controls_tooltip_test.cpp
  controls_tree_list_test.cpp
  default_handlers_test.cpp
  tracking_allocator_test.cpp
  null_backends_test.cpp
  breadcrumb_test.cpp
  node_view_zoom_test.cpp
  vector_field_test.cpp
  command_search_test.cpp
  svg_stroke_test.cpp
  title_bar_test.cpp
  theme_metrics_test.cpp
  table_row_heights_test.cpp
  switch_test.cpp
  number_field_edit_test.cpp
  color_field_hex_test.cpp
  gradient_editor_dialog_test.cpp
  message_dialog_test.cpp
  undo_history_test.cpp
  misuse_test.cpp
  begin_end_test.cpp
  id_stability_test.cpp
  control_reporting_test.cpp
  frame_cost_bench.cpp
  units_test.cpp
  result_test.cpp
  measured_sizes_test.cpp
  ui_state_test.cpp
  icon_names_test.cpp
  fuzz_test.cpp
  node_view_drop_test.cpp
  node_view_lane_offset_test.cpp
  node_view_wire_edit_test.cpp
  node_view_edit_test.cpp
  label_group_alignment_test.cpp
  drag_drop_test.cpp
  harness_self_test.cpp
  frame_pipeline_test.cpp
  frame_clock_test.cpp
  ui_scale_test.cpp
  version_test.cpp
  profiler_test.cpp
  subsystem_test.cpp
  input_manager_test.cpp
  command_manager_test.cpp
  dock_node_test.cpp
  element_storage_test.cpp
  gui_base_types_test.cpp
  color_conversions_test.cpp
  gradient_manager_test.cpp
  gradient_test.cpp
  icon_text_layout_test.cpp
  icon_text_placement_test.cpp
  id_manager_test.cpp
  layout_engine_test.cpp
  logging_test.cpp
  layout_flex_test.cpp
  layout_grid_test.cpp
  layout_text_wrapping_test.cpp
  os_window_manager_test.cpp
  label_handler_test.cpp
  numeric_field_format_test.cpp
  reorderable_focus_test.cpp
  controls_toolbar_dock_test.cpp
  sizer_handler_test.cpp
  sizer_render_test.cpp
  slider_handler_test.cpp
  state_manager_focus_test.cpp
  tab_chain_manager_test.cpp
  focus_ring_test.cpp
  from_chars_test.cpp
  state_manager_test.cpp
  style_geometry_test.cpp
  style_manager_test.cpp
  style_sheet_test.cpp
  table_helpers_test.cpp
  table_integration_test.cpp
  tree_utils_test.cpp
  controls_reorderable_test.cpp
  color_field_test.cpp
  controls_docking_test.cpp
  skribidi_shaping_test.cpp
  font_set_test.cpp
  built_in_resources_test.cpp
  tutorial_test.cpp
  tutorial_snippets_test.cpp
  skribidi_editor_test.cpp
  table_scrollbar_test.cpp
  controls_table_frozen_test.cpp
  controls_table_cells_test.cpp
  controls_table_virtual_test.cpp
  node_view_control_test.cpp
  node_view_hit_test.cpp
  node_view_marquee_test.cpp
  node_view_wire_route_test.cpp
  headless_gui_app.h
  misuse_reports.h
  skribidi_test_fixture.h
  headless_gui_app.cpp
  mock_window_backend.h
  window_registry_test.cpp
  text_editor_coordinate_test.cpp
  ui_cache_test.cpp
  primitive_cache_test.cpp
  pixel_snap_test.cpp
  main.cpp
)

add_executable(gui_core_tests ${GUI_CORE_TEST_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${GUI_CORE_TEST_FILES})
# The tutorial's app (docs/tutorial.md), whose panels tutorial_test.cpp drives.
set(TACHYON_TUTORIAL_SOURCES "${PROJECT_SOURCE_DIR}/examples/tutorial/my_app.cpp" "${PROJECT_SOURCE_DIR}/examples/tutorial/my_app.h")
target_sources(gui_core_tests PRIVATE ${TACHYON_TUTORIAL_SOURCES})
source_group("Tutorial" FILES ${TACHYON_TUTORIAL_SOURCES})

# gui_controls gives us the widget layer (GUIApplication, BeginTable, etc.)
# that integration tests drive through the headless harness. It transitively
# exposes gui_core, so unit tests that only touch the lower layer still work.
target_link_libraries(gui_core_tests PRIVATE gui_controls GTest::gtest_main GTest::gmock)

# Match gui_core/gui_controls so /GL objects don't force a linker restart.
tachyon_apply_compile_options(gui_core_tests)
tachyon_precompile_headers(gui_core_tests <gtest/gtest.h> <gui_controls/gui_application.h>)

# gmock's templated actions trip a benign C4702 (unreachable code). With /GL
# the test exe is code-generated at LINK time (/LTCG), so that warning is
# emitted during whole-program codegen - past the point where /external:W0 (a
# front-end, per-include mechanism) can see it. Disabling C4702 on the test
# target is what actually reaches the LTCG phase (the flag rides along in the
# /GL objects); the only thing lost is unreachable-code detection in test code.
if(MSVC)
  target_compile_options(gui_core_tests PRIVATE /wd4702)
endif()

include(GoogleTest)


# ctest lists the tests when it runs them, not at build time, so building never
# launches the executables (slow on a cold CI runner). It runs in the build folder
# (discovery leaves a JSON file behind); main.cpp switches to workdirectory itself.
tachyon_copy_runtime_dlls(gui_core_tests)
gtest_discover_tests(gui_core_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)

set_target_properties(gui_core_tests PROPERTIES
  FOLDER "Tests"
  VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/workdirectory"
)


# GPU validation harness - real-device Vulkan tests. These boot an actual
# VulkanGraphicsContext with the validation + synchronization-validation layers
# recording into a sink, so backend bugs (descriptor overflow, sync hazards,
# leaks) become test failures. Individual tests SKIP themselves when no suitable
# device/display is present, so the target is safe to build everywhere.
# They drive real windows through SDL.
if(TACHYON_SDL)
  list(APPEND GUI_GPU_TEST_FILES
    gpu/gpu_test_harness.h
    gpu/gpu_descriptor_test.cpp
    gpu/gpu_image_set_test.cpp
    gpu/gpu_window_lifecycle_test.cpp
    gpu/gpu_window_style_test.cpp
    gpu/gpu_primitive_coverage_test.cpp
    gpu/gpu_readback_smoke_test.cpp
    gpu/gpu_external_device_test.cpp
    gpu/gpu_application_init_test.cpp
    gpu/gpu_sdl_host_event_loop_test.cpp
    gpu/gpu_sdl_single_copy_test.cpp
    gpu/gpu_zero_setup_test.cpp
    gpu/gpu_tutorial_test.cpp
    gpu/gpu_crash_trace.cpp
    main.cpp
  )

  add_executable(gui_gpu_tests ${GUI_GPU_TEST_FILES})
  source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${GUI_GPU_TEST_FILES})
  target_sources(gui_gpu_tests PRIVATE ${TACHYON_TUTORIAL_SOURCES})
  source_group("Tutorial" FILES ${TACHYON_TUTORIAL_SOURCES})

  # gui_controls pulls in gui_core; vma/Vulkan/SDL3 are linked PRIVATE by gui_core
  # (a static lib), so the harness must link them directly to reach the backend.
  target_link_libraries(gui_gpu_tests PRIVATE gui_controls vma Vulkan::Vulkan ${TACHYON_SDL_TARGET} vkbootstrap GTest::gtest_main GTest::gmock)
  # The bundled SDL is a static library; a host's SDL (TACHYON_SYSTEM_SDL) may be a DLL.
  target_compile_definitions(gui_gpu_tests PRIVATE TACHYON_TEST_BUNDLED_SDL=$<NOT:$<BOOL:${TACHYON_SYSTEM_SDL}>>)

  tachyon_apply_compile_options(gui_gpu_tests)
  tachyon_precompile_headers(gui_gpu_tests <gtest/gtest.h> <gui_controls/gui_application.h>)

  # Same gmock C4702-at-LTCG situation as gui_core_tests above.
  if(MSVC)
    target_compile_options(gui_gpu_tests PRIVATE /wd4702)
  endif()

  tachyon_copy_runtime_dlls(gui_gpu_tests)
  gtest_discover_tests(gui_gpu_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)

  set_target_properties(gui_gpu_tests PROPERTIES
    FOLDER "Tests"
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/workdirectory"
  )
endif()