# Small standalone programs, one file each. They load the resources from
# workdirectory/ through SetResourceRoot, so they run from any folder.

function(interstice_add_example name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE gui_controls ${ARGN})
  target_compile_definitions(${name} PRIVATE
    INTERSTICE_RESOURCE_DIR="${PROJECT_SOURCE_DIR}/workdirectory"
    INTERSTICE_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  interstice_apply_compile_options(${name})
  set_target_properties(${name} PROPERTIES FOLDER "Examples")
  add_custom_command(TARGET ${name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${name}> $<TARGET_FILE_DIR:${name}>
    COMMAND_EXPAND_LISTS)
endfunction()

interstice_add_example(hello_world)
interstice_add_example(controls)
interstice_add_example(docking)
interstice_add_example(custom_theme)
# The host side creates its own Vulkan device; SDL names the surface extensions.
interstice_add_example(host_device vkbootstrap vma Vulkan::Vulkan SDL3::SDL3)
