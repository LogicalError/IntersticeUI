# Compiles one shader to SPIR-V unless the output is newer than the shader and
# its includes. Visual Studio runs custom build steps once per configuration and
# CI restores compiled shaders from a cache; this keeps both from recompiling.
#   cmake -DVALIDATOR=<glslangValidator> -DSOURCE=<shader> -DOUTPUT=<spv>
#         -DINCLUDES=<a|b|...> -P CompileShader.cmake

string(REPLACE "|" ";" _includes "${INCLUDES}")
if(EXISTS "${OUTPUT}")
  set(_stale FALSE)
  foreach(_input IN LISTS SOURCE _includes)
    if("${_input}" IS_NEWER_THAN "${OUTPUT}")
      set(_stale TRUE)
    endif()
  endforeach()
  if(NOT _stale)
    return()
  endif()
endif()

execute_process(COMMAND "${VALIDATOR}" -V "${SOURCE}" -o "${OUTPUT}" RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
  message(FATAL_ERROR "glslangValidator failed on ${SOURCE}")
endif()
