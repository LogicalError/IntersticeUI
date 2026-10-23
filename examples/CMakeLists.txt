# Small standalone programs, one file each. They load the resources through
# SetResourceRoot(INTERSTICE_RESOURCE_DIR), so they run from any folder.

function(interstice_add_example name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE gui_controls ${ARGN})
  target_compile_definitions(${name} PRIVATE
    INTERSTICE_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  interstice_apply_compile_options(${name})
  set_target_properties(${name} PROPERTIES FOLDER "Examples")
  interstice_copy_runtime_dlls(${name})
endfunction()

interstice_add_example(hello_world)
interstice_add_example(controls)
interstice_add_example(docking)
interstice_add_example(custom_theme)
# The host side creates its own Vulkan device; SDL names the surface extensions.
if(INTERSTICE_SDL)
  interstice_add_example(host_device vkbootstrap vma Vulkan::Vulkan SDL3::SDL3)
endif()
