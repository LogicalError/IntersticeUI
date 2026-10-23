# This file supports two modes:
#
#   1. Add-subdirectory (legacy): the main project pulls us in via
#      add_subdirectory(third_party). No install step runs; the resulting
#      libraries are linked in-tree.
#
#   2. Standalone (prebuilt-cache): this file is used as the top-level
#      CMakeLists.txt via `cmake -S third_party -B <build>`. Install rules
#      then emit a self-contained prefix that the main project consumes
#      via find_package() — see cmake/ThirdPartyCache.cmake.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.22)
  # Debug info goes into the libraries (/Z7), not into separate compile PDBs
  # that the cache and the installed package don't carry (LNK4099 on every
  # link otherwise). Before project(), which picks the default flags by it.
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_POLICY_DEFAULT_CMP0141 NEW)
  project(IntersticeThirdParty LANGUAGES C CXX)

  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_C_STANDARD 17)

  set_property(GLOBAL PROPERTY USE_FOLDERS ON)

  # Only ask sub-projects to emit install artifacts in standalone mode.
  set(SDL_INSTALL ON CACHE BOOL "" FORCE)

  # googletest builds static libs with the non-DLL runtime by default; force
  # the DLL runtime so it matches everything else we link together.
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

  # Multi-config installs collapse Debug and Release artifacts into the same
  # lib/ dir. Without a debug postfix the second install overwrites the first,
  # leading to CRT/iterator-debug mismatches at link time. Default every
  # library target to a `d` Debug postfix — skribidi/budouxc already set their
  # own, so ours just fills in the gaps for harfbuzz, SheenBidi, libunibreak,
  # SDL3, vkbootstrap, etc.
  set(CMAKE_DEBUG_POSTFIX "d" CACHE STRING "" FORCE)

  set(_interstice_tp_standalone TRUE)
else()
  set(_interstice_tp_standalone FALSE)
endif()

find_package(Vulkan REQUIRED)
include(GNUInstallDirs)

# ---- vkbootstrap (static lib) ----
add_library(vkbootstrap STATIC
  vkbootstrap/src/VkBootstrap.h
  vkbootstrap/src/VkBootstrap.cpp
)
target_include_directories(vkbootstrap PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vkbootstrap/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/vkbootstrap>
)
target_link_libraries(vkbootstrap PUBLIC Vulkan::Vulkan $<$<BOOL:UNIX>:${CMAKE_DL_LIBS}>)
set_property(TARGET vkbootstrap PROPERTY CXX_STANDARD 20)

# ---- vma (header-only) ----
add_library(vma INTERFACE)
target_include_directories(vma INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vma/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/vma>
)

# ---- nanosvg (header-only) ----
add_subdirectory(nanosvg)

# ---- Sub-projects ----
# In standalone mode every sub-project must be in ALL so that `--target install`
# actually builds and installs them. In legacy (add_subdirectory) mode we keep
# EXCLUDE_FROM_ALL so the main project doesn't pay for unused artifacts.
if(_interstice_tp_standalone)
  set(_interstice_tp_exclude "")
else()
  set(_interstice_tp_exclude EXCLUDE_FROM_ALL)
endif()

add_subdirectory(SDL ${_interstice_tp_exclude})
# SDL counts its live allocations, so the GUI can tell when it is safe to
# route SDL's memory through a host's allocation hooks.
target_compile_definitions(SDL3-shared PRIVATE SDL_TRACK_ALLOCATION_COUNT)
add_subdirectory(skribidi ${_interstice_tp_exclude})
if(NOT DEFINED INTERSTICE_BUILD_TESTS OR INTERSTICE_BUILD_TESTS)
  add_subdirectory(googletest ${_interstice_tp_exclude})
endif()

# IDE grouping — sit under "ThirdParty" in VS's solution explorer.
# Also force MSVC runtime to /MD(d) for everyone: skribidi's CMakeLists
# sets CMAKE_MSVC_RUNTIME_LIBRARY to /MT inside its scope, which would
# conflict with the engine's default /MD at link time. A per-target
# property override sidesteps patching vendored code.
set(_interstice_third_party_targets
  vma nanosvg SDL_uclibc SDL3-shared vkbootstrap
  skribidi budouxc harfbuzz
  libunibreak SheenBidi
  gtest gtest_main gmock gmock_main
)
set(_interstice_msvc_rt "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
foreach(_tgt IN LISTS _interstice_third_party_targets)
  if(TARGET ${_tgt})
    set_target_properties(${_tgt} PROPERTIES FOLDER "ThirdParty")
    get_target_property(_tgt_type ${_tgt} TYPE)
    if(NOT _tgt_type STREQUAL "INTERFACE_LIBRARY")
      set_target_properties(${_tgt} PROPERTIES MSVC_RUNTIME_LIBRARY ${_interstice_msvc_rt})
      # Ensure every library target has a Debug postfix — some (e.g. SheenBidi)
      # overwrite or ignore CMAKE_DEBUG_POSTFIX, which breaks multi-config
      # installs since Release overwrites Debug at the same lib path.
      get_target_property(_existing_postfix ${_tgt} DEBUG_POSTFIX)
      if(NOT _existing_postfix)
        set_target_properties(${_tgt} PROPERTIES DEBUG_POSTFIX "d")
      endif()
      if(MSVC AND _interstice_tp_standalone)
        set_target_properties(${_tgt} PROPERTIES MSVC_DEBUG_INFORMATION_FORMAT Embedded)
        # skribidi adds /Zi itself, which would override the embedded format.
        get_target_property(_tgt_options ${_tgt} COMPILE_OPTIONS)
        if(_tgt_options)
          list(FILTER _tgt_options EXCLUDE REGEX "/Zi")
          set_target_properties(${_tgt} PROPERTIES COMPILE_OPTIONS "${_tgt_options}")
        endif()
      endif()
    endif()
  endif()
endforeach()

# ---- Install rules (standalone mode only) ----
if(_interstice_tp_standalone)
  # vma — header-only
  install(TARGETS vma EXPORT vmaTargets)
  install(FILES vma/include/vk_mem_alloc.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vma)
  install(EXPORT vmaTargets
    FILE vmaConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vma)

  # vkbootstrap — static
  install(TARGETS vkbootstrap EXPORT vkbootstrapTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES
      vkbootstrap/src/VkBootstrap.h
      vkbootstrap/src/VkBootstrapDispatch.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vkbootstrap)
  install(EXPORT vkbootstrapTargets
    FILE vkbootstrapConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vkbootstrap)

  # skribidi installs its library via its own install() calls, but its
  # `if(PROJECT_IS_TOP_LEVEL)` guard skips installing the public headers
  # when included via add_subdirectory. Install them explicitly here
  # (filter by pattern so stray editor dirs like .idea are skipped).
  install(DIRECTORY skribidi/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
      PATTERN "*.h"
      PATTERN ".*" EXCLUDE)

  # SheenBidi's own install rule is gated to `CONFIGURATIONS Release`, so
  # a Debug build of the main engine would otherwise pull the Release .lib
  # and blow up with iterator/CRT mismatches. We can't re-add SheenBidi to
  # its own EXPORT set, so: install the Debug .lib directly and drop a
  # handwritten SheenBidiTargets-debug.cmake next to the Release one.
  # SheenBidiTargets.cmake (installed by sheenbidi) globs *-<config>.cmake
  # automatically. The shim file is content-identical across configs, so
  # install it unconditionally — otherwise the Release install's empty
  # auto-generated -debug.cmake clobbers our good one.
  if(TARGET SheenBidi)
    install(FILES $<TARGET_FILE:SheenBidi>
      CONFIGURATIONS Debug
      DESTINATION ${CMAKE_INSTALL_LIBDIR})

    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/SheenBidiTargets-debug.cmake"
"# Injected by IntersticeThirdParty: SheenBidi's own install rule is Release-only.\n"
"set_property(TARGET SheenBidi::SheenBidi APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)\n"
"set_target_properties(SheenBidi::SheenBidi PROPERTIES\n"
"  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG \"C\"\n"
"  IMPORTED_LOCATION_DEBUG \"\${_IMPORT_PREFIX}/lib/SheenBidid.lib\"\n"
")\n")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SheenBidiTargets-debug.cmake"
      DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SheenBidi)
  endif()

  # SDL3 installs itself when SDL_INSTALL is ON (which we force above). Any
  # fixup it needs is applied by cmake/ThirdPartyCache.cmake at find_package
  # time rather than by rewriting the installed files — install(EXPORT)'s
  # "replace" heuristic will wipe per-config files if we make the main file
  # look suspicious, so we leave it alone.
endif()
