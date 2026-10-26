# Compiler/linker options for first-party targets (gui_lib, gui_controls, engine).
# Third-party targets keep whatever flags their own build systems select.
#
# MSVC Release-mode choices aim for debuggable/profileable builds:
#   /GL + /LTCG   - whole-program optimisation, only with INTERSTICE_LTCG: it makes
#                   every link redo code generation, and /GL objects in the
#                   installed libraries force /LTCG and the exact compiler version
#                   on consumers
#   /Zi           - emit a PDB for Release
#   /Zo           - enhanced debug info for optimised code (locals/inlined frames)
#   /d2Zi+        - richer optimised-code debug info (undocumented; widely used)
#   /DEBUG        - linker: keep emitting the PDB
#   /OPT:REF,ICF  - linker: still strip unused and fold duplicate COMDATs despite /DEBUG

option(INTERSTICE_LTCG "Whole-program optimisation (/GL + /LTCG) in Release" OFF)
option(INTERSTICE_PRECOMPILED_HEADERS "Precompile the headers most sources include" ON)

# Precompiles headers most of the target's sources include. Turn the option off
# to check that every file includes what it uses.
function(interstice_precompile_headers target)
  if(INTERSTICE_PRECOMPILED_HEADERS)
    target_precompile_headers(${target} PRIVATE ${ARGN})
  endif()
endfunction()

function(interstice_apply_compile_options target)
  if(MSVC)
    target_compile_options(${target} PRIVATE
      /MP
      /W4
      /wd4201                            # allow nameless struct/union (widely-used MS extension)
      /EHsc                              # standard C++ unwind semantics (silences C4530)
      /utf-8                             # sources and narrow literals are UTF-8 (default is the system code page)

      # Keep /W4 on our code but silence FRONT-END warnings from third-party
      # headers (gmock, SDL, Vulkan, VMA, ...). CMake marks imported/SYSTEM
      # include dirs with /external:I; /external:W0 sets their warning level to
      # 0. Only header dirs CMake classifies as external are affected - our own
      # /W4 is untouched. NOTE: this is a front-end, per-include mechanism, so
      # it cannot suppress warnings emitted during /LTCG whole-program code
      # generation (those carry no include context) - gmock's C4702 is one such
      # case, handled by /wd4702 on the test targets instead.
      /external:W0

      # Debug: full debug info, no optimisation, runtime checks. Applied at the
      # target level rather than relying on CMAKE_CXX_FLAGS_DEBUG, which has
      # been observed to come through empty in some cache states (the C
      # variant ends up populated but CXX does not, leaving the generated
      # vcxproj with empty <DebugInformationFormat> / <BasicRuntimeChecks>
      # tags and an undebuggable Debug build).
      $<$<CONFIG:Debug>:/Zi>
      $<$<CONFIG:Debug>:/Od>
      $<$<CONFIG:Debug>:/Ob0>
      $<$<CONFIG:Debug>:/RTC1>

      $<$<AND:$<CONFIG:Release>,$<BOOL:${INTERSTICE_LTCG}>>:/GL>
      $<$<CONFIG:Release>:/Zi>
      $<$<CONFIG:Release>:/Zo>
      $<$<CONFIG:Release>:/d2Zi+>
    )

    # _DEBUG is normally part of CMAKE_CXX_FLAGS_DEBUG; set it here in case
    # that variable is empty for the same reason as above.
    target_compile_definitions(${target} PRIVATE
      $<$<CONFIG:Debug>:_DEBUG>
    )

    target_link_options(${target} PRIVATE
      $<$<CONFIG:Debug>:/DEBUG>
      # Clean (non-incremental) link. With /Ob0 every header inline is an
      # out-of-line COMDAT; incremental linking can fold a stale copy when only
      # some TUs recompiled, diverging the counter-based ID order across the
      # binary (scroll keys mismatch -> tables reset). Clean links stay consistent.
      $<$<CONFIG:Debug>:/INCREMENTAL:NO>

      $<$<AND:$<CONFIG:Release>,$<BOOL:${INTERSTICE_LTCG}>>:/LTCG>
      $<$<CONFIG:Release>:/DEBUG>
      $<$<CONFIG:Release>:/OPT:REF>
      $<$<CONFIG:Release>:/OPT:ICF>
    )
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # -Wpedantic would reject nameless struct/union, which we rely on. Designated
    # initializers leave fields at their defaults on purpose; GCC still reports them.
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wno-missing-field-initializers
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      target_compile_options(${target} PRIVATE
        -Wno-gnu-anonymous-struct
        -Wno-nested-anon-types
        -Wno-missing-braces    # IntRect{ x, y, w, h } fills its anonymous union by brace elision
      )
    endif()
  endif()
endfunction()

# Copies a program's runtime DLLs (SDL3) next to it so it can be launched. A
# build without SDL has none, and copy_if_different fails on an empty list.
function(interstice_copy_runtime_dlls target)
  set(dlls "$<TARGET_RUNTIME_DLLS:${target}>")
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "$<$<BOOL:${dlls}>:${CMAKE_COMMAND};-E;copy_if_different;${dlls};$<TARGET_FILE_DIR:${target}>>"
    COMMAND_EXPAND_LISTS)
endfunction()
