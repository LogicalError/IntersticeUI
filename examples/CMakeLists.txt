# The examples, one folder each. The small ones are standalone programs that load the
# resources through SetResourceRoot(TACHYON_RESOURCE_DIR), so they run from any folder;
# the showcase is a larger editor-style app that runs from workdirectory/.

# tachyon_add_example(<name> <sources...> [LIBS <libraries...>]): an example executable in
# the current folder. TACHYON_EXAMPLE_DIR is that folder, for the example's own files.
function(tachyon_add_example name)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "LIBS")
  add_executable(${name} ${ARG_UNPARSED_ARGUMENTS})
  target_link_libraries(${name} PRIVATE gui_controls ${ARG_LIBS})
  target_compile_definitions(${name} PRIVATE
    TACHYON_EXAMPLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  tachyon_apply_compile_options(${name})
  set_target_properties(${name} PROPERTIES FOLDER "Examples")
  tachyon_copy_runtime_dlls(${name})
endfunction()

if(TACHYON_BUILD_EXAMPLES)
  add_subdirectory(hello_world)
  add_subdirectory(controls)
  add_subdirectory(docking)
  add_subdirectory(custom_theme)
  # The host side creates its own Vulkan device; SDL names the surface extensions.
  if(TACHYON_SDL)
    add_subdirectory(host_device)
  endif()
endif()

if(TACHYON_BUILD_SHOWCASE)
  add_subdirectory(showcase)
endif()
