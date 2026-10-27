# Small standalone programs, one file each. They load the resources through
# SetResourceRoot(TACHYON_RESOURCE_DIR), so they run from any folder.

function(tachyon_add_example name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE gui_controls ${ARGN})
  target_compile_definitions(${name} PRIVATE
    TACHYON_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  tachyon_apply_compile_options(${name})
  set_target_properties(${name} PROPERTIES FOLDER "Examples")
  tachyon_copy_runtime_dlls(${name})
endfunction()

tachyon_add_example(hello_world)
tachyon_add_example(controls)
tachyon_add_example(docking)
tachyon_add_example(custom_theme)
# The host side creates its own Vulkan device; SDL names the surface extensions.
if(TACHYON_SDL)
  tachyon_add_example(host_device vkbootstrap vma Vulkan::Vulkan SDL3::SDL3)
endif()
