# Compiles GLSL sources under workdirectory/shaders into SPIR-V, in
# ${TACHYON_BUILD_RESOURCE_DIR}/shaders_compiled. Creates the
# `tachyon_shaders` custom target that gui_core depends on; inside another
# project also `tachyon_resources`, which copies the other resources next to them.

find_program(GLSL_VALIDATOR glslangValidator
  HINTS /usr/bin /usr/local/bin $ENV{VULKAN_SDK}/Bin/ $ENV{VULKAN_SDK}/Bin32/ $ENV{VULKAN_SDK}/bin/)
if(NOT GLSL_VALIDATOR)
  message(FATAL_ERROR
    "glslangValidator not found. Install the Vulkan SDK (and set VULKAN_SDK), "
    "or point GLSL_VALIDATOR at the executable.")
endif()

file(GLOB_RECURSE GLSL_SOURCE_FILES
  CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/workdirectory/shaders/*.frag"
  "${PROJECT_SOURCE_DIR}/workdirectory/shaders/*.vert"
  "${PROJECT_SOURCE_DIR}/workdirectory/shaders/*.comp"
)

# Every shader depends on every include (all_common.glsl includes bezier_sdf.glsl,
# for one), so a changed include recompiles them all.
file(GLOB GLSL_INCLUDE_FILES
  CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/workdirectory/shaders/*.glsl"
)

# The includes as one argument for CompileShader.cmake.
string(REPLACE ";" "|" _tachyon_shader_includes "${GLSL_INCLUDE_FILES}")
set(SPIRV_BINARY_FILES "")
foreach(GLSL ${GLSL_SOURCE_FILES})
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${TACHYON_BUILD_RESOURCE_DIR}/shaders_compiled/${FILE_NAME}.spv")
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${CMAKE_COMMAND} -DVALIDATOR=${GLSL_VALIDATOR} -DSOURCE=${GLSL} -DOUTPUT=${SPIRV}
            "-DINCLUDES=${_tachyon_shader_includes}" -P ${CMAKE_CURRENT_LIST_DIR}/CompileShader.cmake
    DEPENDS ${GLSL} ${GLSL_INCLUDE_FILES}
    COMMENT "  > > >  BUILDING SHADER > > >  ${GLSL}"
    VERBATIM)
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach()

add_custom_target(tachyon_shaders ALL
  SOURCES ${GLSL_SOURCE_FILES} ${GLSL_INCLUDE_FILES}
  DEPENDS ${SPIRV_BINARY_FILES} ${GLSL_SOURCE_FILES} ${GLSL_INCLUDE_FILES}
  COMMAND ${CMAKE_COMMAND} -E echo "...Building Shaders..."
)

set_source_files_properties(${SPIRV_BINARY_FILES} PROPERTIES GENERATED TRUE)
set_target_properties(tachyon_shaders PROPERTIES FOLDER "Examples")

if(NOT PROJECT_IS_TOP_LEVEL)
  # file(COPY) skips files that haven't changed, so this is cheap after the first build.
  add_custom_target(tachyon_resources ALL
    COMMAND ${CMAKE_COMMAND} "-DSOURCE=${PROJECT_SOURCE_DIR}/workdirectory"
            "-DDESTINATION=${TACHYON_BUILD_RESOURCE_DIR}" "-DFALLBACK_FONTS=${TACHYON_FALLBACK_FONTS}"
            -P ${CMAKE_CURRENT_LIST_DIR}/CopyResources.cmake
    VERBATIM)
  set_target_properties(tachyon_resources PROPERTIES FOLDER "Examples")
endif()
