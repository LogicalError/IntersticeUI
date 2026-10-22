# Compiles GLSL sources under workdirectory/shaders into SPIR-V.
# Creates a `Shaders` custom target that the engine depends on.

find_program(GLSL_VALIDATOR glslangValidator
  HINTS /usr/bin /usr/local/bin $ENV{VULKAN_SDK}/Bin/ $ENV{VULKAN_SDK}/Bin32/)

file(GLOB_RECURSE GLSL_SOURCE_FILES
  CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/workdirectory/shaders/*.frag"
  "${PROJECT_SOURCE_DIR}/workdirectory/shaders/*.vert"
  "${PROJECT_SOURCE_DIR}/workdirectory/shaders/*.comp"
)

set(GLSL_INCLUDE_FILES
  "${PROJECT_SOURCE_DIR}/workdirectory/shaders/all_common.glsl"
  "${PROJECT_SOURCE_DIR}/workdirectory/shaders/sdf_color.glsl"
  "${PROJECT_SOURCE_DIR}/workdirectory/shaders/box_common.glsl"
  "${PROJECT_SOURCE_DIR}/workdirectory/shaders/dot_common.glsl"
  "${PROJECT_SOURCE_DIR}/workdirectory/shaders/glyph_common.glsl"
  "${PROJECT_SOURCE_DIR}/workdirectory/shaders/line_common.glsl"
  "${PROJECT_SOURCE_DIR}/workdirectory/shaders/triangle_common.glsl"
  "${PROJECT_SOURCE_DIR}/workdirectory/shaders/quad_common.glsl"
)

set(SPIRV_BINARY_FILES "")
foreach(GLSL ${GLSL_SOURCE_FILES})
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_SOURCE_DIR}/workdirectory/shaders_compiled/${FILE_NAME}.spv")
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL} ${GLSL_INCLUDE_FILES}
    COMMENT "  > > >  BUILDING SHADER > > >  ${GLSL}")
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach()

add_custom_target(Shaders ALL
  SOURCES ${GLSL_SOURCE_FILES} ${GLSL_INCLUDE_FILES}
  DEPENDS ${SPIRV_BINARY_FILES} ${GLSL_SOURCE_FILES} ${GLSL_INCLUDE_FILES}
  COMMAND ${CMAKE_COMMAND} -E echo "...Building Shaders..."
)

set_source_files_properties(${SPIRV_BINARY_FILES} PROPERTIES GENERATED TRUE)
set_target_properties(Shaders PROPERTIES FOLDER "Core")
