# GUI Library - Static library containing all low-level GUI code
list(APPEND GUI_FILES
  # Core
  gui_core/gui_base_types.h
  gui_core/gui_base_types.cpp

  gui_core/gui_enum_operators.h
  gui_core/gui_hash.h

  gui_core/gui_id.h
  gui_core/gui_id_manager.h
  gui_core/gui_id_manager.cpp
  gui_core/gui_from_chars.h
  gui_core/gui_json.h
  gui_core/gui_json.cpp

  gui_core/gui_platform.h
  gui_core/gui_resources.h
  gui_core/gui_resources.cpp
  gui_core/gui_embedded_files_internal.h
  gui_core/gui_result.h
  gui_core/gui_units.h


  # Frame
  gui_core/frame/gui_frame_pipeline.h
  gui_core/frame/gui_frame_pipeline.cpp
  gui_core/frame/gui_frame_clock.h
  gui_core/frame/gui_frame_clock.cpp


  # Diagnostics
  gui_core/diagnostics/gui_profiler.h
  gui_core/diagnostics/gui_profiler.cpp
  gui_core/diagnostics/gui_logging.h
  gui_core/diagnostics/gui_logging.cpp

  # Memory
  gui_core/memory/gui_allocation_hooks.h
  gui_core/memory/gui_tracking_allocator.h
  gui_core/memory/gui_tracking_allocator.cpp


  # State
  gui_core/state/gui_tab_chain_manager.cpp
  gui_core/state/gui_tab_chain_manager.h
  gui_core/state/gui_state_atomic_values.h
  gui_core/state/gui_scroll_management.cpp
  gui_core/state/gui_scroll_management.h
  gui_core/state/gui_interaction_state.h
  gui_core/state/gui_interaction_state.cpp
  gui_core/state/gui_drag_drop_state.h
  gui_core/state/gui_drag_drop_state.cpp
  gui_core/state/gui_state_manager.h
  gui_core/state/gui_state_manager.cpp

  # Elements
  gui_core/elements/gui_element_flags.h
  gui_core/elements/gui_element.h
  gui_core/elements/gui_element_iterators.h
  gui_core/elements/gui_element_iterators.cpp
  gui_core/elements/gui_element_storage.cpp
  gui_core/elements/gui_element_storage.h
  gui_core/elements/gui_element_data_store.h
  gui_core/elements/gui_element_data_store.cpp

  
  # Layout  
  gui_core/layout/gui_constraints.h
  gui_core/layout/gui_constraints.cpp
  gui_core/layout/gui_layout_engine.cpp
  gui_core/layout/gui_layout_engine.h
  gui_core/layout/gui_layout_flexbox.cpp
  gui_core/layout/gui_layout_flexbox.h
  gui_core/layout/gui_layout_grid.cpp
  gui_core/layout/gui_layout_grid.h
  gui_core/layout/gui_layout_helpers.cpp
  gui_core/layout/gui_layout_helpers.h


  # Style
  gui_core/style/gui_style.h
  gui_core/style/gui_style.cpp
  gui_core/style/gui_style_ref.h
  gui_core/style/gui_style_text.h
  gui_core/style/gui_style_text.cpp
  gui_core/style/gui_color_conversions.h
  gui_core/style/gui_style_gradient.h
  gui_core/style/gui_style_gradient.cpp
  gui_core/style/gui_style_manager.cpp
  gui_core/style/gui_style_manager.h
  gui_core/style/gui_gradient_manager.cpp
  gui_core/style/gui_gradient_manager.h
  gui_core/style/gui_style_sheet.h
  gui_core/style/gui_style_sheet_internal.h
  gui_core/style/gui_style_sheet.cpp
  gui_core/style/gui_style_sheet_properties.cpp


  # Input
  gui_core/input/gui_input_state.h
  gui_core/input/gui_input_enums.h
  gui_core/input/gui_input_manager.h
  gui_core/input/gui_undo_history.h
  gui_core/input/gui_input_manager.cpp
  gui_core/input/gui_platform_input_handler.h
  gui_core/input/gui_input_handler_func.h
  gui_core/input/gui_pointer_drag.h
  gui_core/input/gui_pointer_drag.cpp
  gui_core/input/gui_input_handler_broadcaster.h
  gui_core/input/gui_command_manager.h


  # Windows
  gui_core/windows/gui_window_registry.h
  gui_core/windows/gui_window_flags.h
  gui_core/windows/gui_window_backend.h
  gui_core/windows/gui_os_window_manager.cpp
  gui_core/windows/gui_image_window_backend.h
  gui_core/windows/gui_image_window_backend.cpp
  gui_core/windows/null/gui_null_window_backend.h
  gui_core/windows/null/gui_null_window_backend.cpp
  gui_core/windows/gui_os_window_manager.h
  gui_core/windows/gui_window_sync.h
  gui_core/windows/gui_window_sync.cpp
  
  
  # Text
  gui_core/text/gui_numeric_field_config.h
  gui_core/text/gui_numeric_type.h
  gui_core/text/gui_numeric_text.h
  gui_core/text/gui_numeric_text.cpp
  gui_core/text/gui_text_converter.h
  gui_core/text/gui_utf8.h
  gui_core/text/gui_icon_text_placement.h
  gui_core/text/gui_text_atlas_manager.h
  gui_core/text/gui_text_system.h
  gui_core/text/gui_clipboard.h


  # Text/Skribidi Backend
  gui_core/text/skribidi/gui_default_fonts_skribidi.cpp
  gui_core/text/skribidi/gui_text_system_skribidi.cpp
  gui_core/text/skribidi/gui_text_system_skribidi.h
  gui_core/text/skribidi/gui_text_editor_system_skribidi.cpp
  gui_core/text/skribidi/gui_text_editor_registration_skribidi.cpp
  gui_core/text/skribidi/gui_text_editor_input_skribidi.cpp
  gui_core/text/skribidi/internal/gui_skribidi_context.cpp
  gui_core/text/skribidi/internal/gui_skribidi_context.h
  gui_core/text/skribidi/internal/gui_default_fonts_skribidi.h
  gui_core/text/skribidi/internal/nanosvg_bridge.cpp
  gui_core/text/skribidi/internal/nanosvg_bridge.h
  


  # Graphics
  gui_core/graphics/gui_buffer.h

  gui_core/graphics/gui_custom_draw_context.h
  gui_core/graphics/gui_custom_draw_context.cpp

  gui_core/graphics/gui_graphics_context.h

  gui_core/graphics/gui_image.h

  gui_core/graphics/gui_primitive_renderer.h
  gui_core/graphics/gui_primitive_renderer.cpp

  gui_core/graphics/gui_render_primitives.h
  gui_core/graphics/gui_pixel_snap.h

  gui_core/graphics/gui_renderer.h
  gui_core/graphics/gui_renderer.cpp

  gui_core/graphics/gui_rhi.h

  gui_core/graphics/null/gui_null_graphics_context.h
  gui_core/graphics/null/gui_null_graphics_context.cpp


  # Graphics/Vulkan Backend
  gui_core/graphics/vulkan/vk_defaults.h

  gui_core/graphics/vulkan/vk_descriptors.cpp
  gui_core/graphics/vulkan/vk_descriptors.h

  gui_core/graphics/vulkan/vk_device_context.cpp
  gui_core/graphics/vulkan/vk_device_context.h

  gui_core/graphics/vulkan/vk_graphics_context.cpp
  gui_core/graphics/vulkan/vk_graphics_context.h

  gui_core/graphics/vulkan/vk_image.h
  gui_core/graphics/vulkan/vk_images.cpp
  gui_core/graphics/vulkan/vk_images.h

  gui_core/graphics/vulkan/vk_initializers.cpp
  gui_core/graphics/vulkan/vk_initializers.h

  gui_core/graphics/vulkan/vk_meshes.h

  gui_core/graphics/vulkan/vk_pipelines.cpp
  gui_core/graphics/vulkan/vk_pipelines.h


  gui_core/graphics/vulkan/vk_rhi.cpp
  gui_core/graphics/vulkan/vk_rhi.h

  gui_core/graphics/vulkan/vk_text_atlas_manager.cpp
  gui_core/graphics/vulkan/vk_text_atlas_manager.h

  gui_core/graphics/vulkan/vk_types.h

  gui_core/graphics/vulkan/vk_upload_worker.cpp
  gui_core/graphics/vulkan/vk_upload_worker.h

  gui_core/graphics/vulkan/vk_window_context.cpp
  gui_core/graphics/vulkan/vk_window_context.h
)

# Create GUI static library
# The SDL window backend and clipboard (TACHYON_SDL).
if(TACHYON_SDL)
  list(APPEND GUI_FILES
    gui_core/windows/sdl/gui_window_backend_sdl_vulkan.cpp
    gui_core/windows/sdl/gui_window_backend_sdl_vulkan.h
    gui_core/windows/sdl/gui_window_backend_sdl.cpp
    gui_core/windows/sdl/gui_window_backend_sdl.h
    gui_core/windows/sdl/gui_sdl_clipboard.cpp
    gui_core/windows/sdl/gui_sdl_clipboard.h)
endif()

# Files compiled into gui_core: the default themes and fonts (TACHYON_EMBED_RESOURCES), and
# the Vulkan backend's shaders.
set(_tachyon_embedded_resources
  themes/dark.css
  themes/light.css
  themes/styles.css
  fonts/Inter/Inter-Regular.ttf
  fonts/Inter/Inter-Medium.ttf
  fonts/Inter/Inter-SemiBold.ttf
  fonts/Inter/Inter-Bold.ttf
  fonts/Inter/Inter-Italic.ttf
  fonts/Inter/Inter-BoldItalic.ttf
  fonts/JetBrainsMono/JetBrainsMono-Regular.ttf
  fonts/JetBrainsMono/JetBrainsMono-Bold.ttf)
set(_tachyon_resource_entries "")
set(_tachyon_resource_inputs "")
if(TACHYON_EMBED_RESOURCES)
  foreach(_resource IN LISTS _tachyon_embedded_resources)
    list(APPEND _tachyon_resource_entries "${_resource}=${PROJECT_SOURCE_DIR}/workdirectory/${_resource}")
    list(APPEND _tachyon_resource_inputs "${PROJECT_SOURCE_DIR}/workdirectory/${_resource}")
  endforeach()
endif()
set(_tachyon_shader_entries "")
foreach(_spirv IN LISTS SPIRV_BINARY_FILES)
  get_filename_component(_spirv_name "${_spirv}" NAME)
  list(APPEND _tachyon_shader_entries "${_spirv_name}=${_spirv}")
endforeach()

set(_tachyon_resources_source "${PROJECT_BINARY_DIR}/generated/gui_core/gui_embedded_resources.cpp")
set(_tachyon_shaders_source   "${PROJECT_BINARY_DIR}/generated/gui_core/vk_embedded_shaders.cpp")
string(REPLACE ";" "|" _tachyon_resource_arg "${_tachyon_resource_entries}")
string(REPLACE ";" "|" _tachyon_shader_arg "${_tachyon_shader_entries}")
add_custom_command(
  OUTPUT "${_tachyon_resources_source}"
  COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${_tachyon_resources_source}" -DFUNCTION=EmbeddedResourceFiles
          "-DFILES=${_tachyon_resource_arg}" -P "${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake"
  DEPENDS ${_tachyon_resource_inputs} "${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake"
  COMMENT "Compiling the default themes and fonts into gui_core"
  VERBATIM)
add_custom_command(
  OUTPUT "${_tachyon_shaders_source}"
  COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${_tachyon_shaders_source}" -DFUNCTION=EmbeddedShaderFiles
          "-DFILES=${_tachyon_shader_arg}" -P "${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake"
  DEPENDS ${SPIRV_BINARY_FILES} "${PROJECT_SOURCE_DIR}/cmake/EmbedFiles.cmake"
  COMMENT "Compiling the shaders into gui_core"
  VERBATIM)
add_library(gui_core STATIC ${GUI_FILES} "${_tachyon_resources_source}" "${_tachyon_shaders_source}")
source_group("Generated" FILES "${_tachyon_resources_source}" "${_tachyon_shaders_source}")

# Version header, generated from the project version.
configure_file(gui_core/gui_version.h.in "${PROJECT_BINARY_DIR}/generated/gui_core/gui_version.h" @ONLY)

target_include_directories(gui_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
  $<INSTALL_INTERFACE:include>)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/.. FILES ${GUI_FILES})

# vma and the Vulkan headers are PUBLIC because the public device-context headers
# expose Vulkan and VMA types (host-provided device). Everything else is an
# implementation detail of the static library.
target_link_libraries(gui_core
  PUBLIC  vma Vulkan::Headers
  PRIVATE Vulkan::Vulkan vkbootstrap skribidi nanosvg)
if(TACHYON_SDL)
  target_link_libraries(gui_core PRIVATE ${TACHYON_SDL_TARGET})
  if(APPLE)   # the cursor size from the universal-access preferences
    target_link_libraries(gui_core PRIVATE "-framework CoreFoundation")
  endif()
endif()
# PUBLIC, so a host can tell whether the SDL backend is there.
target_compile_definitions(gui_core PUBLIC TACHYON_SDL=$<BOOL:${TACHYON_SDL}>)

#target_precompile_headers(gui_core PUBLIC <optional> <string> <vector> <unordered_map> <memory> )


# GUI Controls - Widget toolkit library (controls, fields, frame handling, menu manager)
list(APPEND GUI_CONTROLS_FILES
  gui_controls/gui_application.h
  gui_controls/gui_application_internal.h
  gui_controls/gui_application.cpp
  gui_controls/gui_content.h
  gui_controls/gui_widget_state_types.h
  gui_controls/gui_node_view_types.h
  gui_controls/gui_table_types.h
  gui_controls/gui_tree_grid_types.h
  gui_controls/gui_docking_types.h
  gui_controls/gui_menu_manager.h
  gui_controls/gui_menu_manager.cpp

  gui_controls/subsystems/gui_subsystem_state.h

  gui_controls/subsystems/gui_label_group_subsystem.h
  gui_controls/subsystems/gui_label_group_subsystem.cpp

  gui_controls/subsystems/gui_button_group_subsystem.h
  gui_controls/subsystems/gui_button_group_subsystem.cpp

  gui_controls/subsystems/gui_color_picker_subsystem.h
  gui_controls/subsystems/gui_color_picker_subsystem.cpp

  gui_controls/subsystems/gui_gradient_editor_subsystem.h
  gui_controls/subsystems/gui_gradient_editor_subsystem.cpp

  gui_controls/subsystems/gui_reorderable_subsystem.h
  gui_controls/subsystems/gui_reorderable_subsystem.cpp

  gui_controls/subsystems/gui_ui_cache_subsystem.h
  gui_controls/subsystems/gui_ui_cache_subsystem.cpp

  gui_controls/gui_dock_node.h
  gui_controls/gui_dock_node.cpp

  gui_controls/gui_table_helpers.h
  gui_controls/gui_table_helpers.cpp

  gui_controls/gui_theme.h
  gui_controls/gui_theme.cpp

  gui_controls/gui_default_handlers.h
  gui_controls/gui_default_handlers.cpp
  gui_controls/gui_sizer_handlers.cpp
  gui_controls/gui_label_handlers.cpp

  gui_controls/gui_type_dimensions.h

  gui_controls/gui_tooltip_manager.h
  gui_controls/gui_tooltip_manager.cpp
  
  gui_controls/gui_label_binding.h
  gui_controls/gui_label_binding.cpp

  gui_controls/gui_transition_manager.h
  gui_controls/gui_transition_manager.cpp

  gui_controls/gui_node_view_helpers.h
  gui_controls/gui_node_view_helpers.cpp

  gui_controls/gui_tree_utils.h

  gui_controls/controls.cpp
  gui_controls/controls_color_internal.h
  gui_controls/controls_color.cpp
  gui_controls/controls_message_box.cpp
  gui_controls/controls_gradient.cpp
  gui_controls/controls_menus.cpp
  gui_controls/controls_node_view.cpp
  gui_controls/controls_tooltips_menus.cpp
  gui_controls/controls_reorderable.cpp
  gui_controls/controls_scrollbar.cpp
  gui_controls/controls_tree_list.cpp
  gui_controls/controls_grid_view.cpp
  gui_controls/controls_table.cpp
  gui_controls/controls_table_cells.cpp
  gui_controls/controls_table_handlers.cpp
  gui_controls/controls_table_virtual.cpp
  gui_controls/controls_table_internal.h
  gui_controls/controls_tabs.cpp
  gui_controls/controls_docking.cpp
  gui_controls/controls_settings.cpp
  gui_controls/gui_ui_state.cpp
  gui_controls/controls_toolbar_dock.h
  gui_controls/controls_toolbar_dock.cpp
)

add_library(gui_controls STATIC ${GUI_CONTROLS_FILES})

# The same names as the installed package, for a host that adds this project.
add_library(TachyonUI::gui_core ALIAS gui_core)
add_library(TachyonUI::gui_controls ALIAS gui_controls)

# Where GUIApplication::SetResourceRoot finds the resources in this build; the
# installed package defines it for its own folder.
target_compile_definitions(gui_controls INTERFACE
  $<BUILD_INTERFACE:TACHYON_RESOURCE_DIR="${TACHYON_BUILD_RESOURCE_DIR}">)

target_include_directories(gui_controls PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/.. FILES ${GUI_CONTROLS_FILES})

target_link_libraries(gui_controls PUBLIC gui_core)


tachyon_apply_compile_options(gui_core)
option(TACHYON_VMA_IMPLEMENTATION "Compile VMA's implementation into gui_core; off when the host links its own VMA" ON)
target_compile_definitions(gui_core PRIVATE TACHYON_VMA_IMPLEMENTATION=$<BOOL:${TACHYON_VMA_IMPLEMENTATION}>)
tachyon_apply_compile_options(gui_controls)
tachyon_precompile_headers(gui_core <algorithm> <array> <cmath> <functional> <memory> <span> <string> <string_view> <unordered_map> <unordered_set> <vector>)
tachyon_precompile_headers(gui_controls <algorithm> <array> <cmath> <functional> <memory> <span> <string> <string_view> <unordered_map> <unordered_set> <vector> <gui_controls/gui_application.h>)
set_target_properties(gui_core gui_controls PROPERTIES FOLDER "Core")
