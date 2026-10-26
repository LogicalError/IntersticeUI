# Resolves third-party dependencies from a prebuilt install prefix.
#
# On first configure (or when a config's stamp is missing) this runs a nested
# CMake against `third_party/CMakeLists.txt`, builds it, and installs to
# `${PROJECT_BINARY_DIR}/third_party/install`. From then on the main build just
# does find_package() against that prefix - third-party is never recompiled
# just because our code changes. Deleting the root `build/` tree does wipe
# the cache, so a clean reconfigure pays the third-party build cost once.
#
# To force a rebuild without wiping `build/`: set `-DINTERSTICE_THIRD_PARTY_REBUILD=ON`.

set(INTERSTICE_TP_INSTALL "${PROJECT_BINARY_DIR}/third_party/install" CACHE PATH
  "Install prefix for prebuilt third-party libraries")

set(INTERSTICE_TP_BUILD "${PROJECT_BINARY_DIR}/third_party/build" CACHE PATH
  "Build tree for the third-party superproject")

option(INTERSTICE_THIRD_PARTY_REBUILD
  "Wipe the third-party install prefix and rebuild from source" OFF)

if(INTERSTICE_THIRD_PARTY_REBUILD)
  message(STATUS "[third-party] INTERSTICE_THIRD_PARTY_REBUILD=ON — clearing cache")
  file(REMOVE_RECURSE "${INTERSTICE_TP_INSTALL}")
  file(REMOVE_RECURSE "${INTERSTICE_TP_BUILD}")
  set(INTERSTICE_THIRD_PARTY_REBUILD OFF CACHE BOOL "" FORCE)
endif()

# Which configs do we need to have installed?
# Multi-config generators (VS, Ninja Multi-Config) let the user pick at build
# time, so we need every listed configuration available in the install prefix.
# Default to Debug;Release - rebuilding MinSizeRel/RelWithDebInfo too would
# double first-configure time for little benefit. Users who want those can
# override INTERSTICE_TP_CONFIGS at configure time.
set(INTERSTICE_TP_CONFIGS "Debug;Release" CACHE STRING
  "Configurations to prebuild the third-party cache for (multi-config only)")

if(CMAKE_CONFIGURATION_TYPES)
  set(_interstice_tp_configs ${INTERSTICE_TP_CONFIGS})
else()
  if(CMAKE_BUILD_TYPE)
    set(_interstice_tp_configs ${CMAKE_BUILD_TYPE})
  else()
    set(_interstice_tp_configs Release)
  endif()
endif()

# What the cache was built for; a cache built for other choices is rebuilt.
set(_interstice_tp_signature
  "sdl=${INTERSTICE_SDL};system-sdl=${INTERSTICE_SYSTEM_SDL};system-harfbuzz=${INTERSTICE_SYSTEM_HARFBUZZ};system-vma=${INTERSTICE_SYSTEM_VMA}")

function(_interstice_build_third_party_config cfg)
  set(stamp "${INTERSTICE_TP_INSTALL}/.stamp-${cfg}")
  if(EXISTS "${stamp}")
    file(READ "${stamp}" _stamp_text)
    if(NOT _stamp_text STREQUAL "${_interstice_tp_signature}")
      file(REMOVE "${stamp}")
    endif()
  endif()
  # A cache built with the tests off has no googletest: rebuild once they're on.
  if(INTERSTICE_BUILD_TESTS AND NOT EXISTS "${INTERSTICE_TP_INSTALL}/lib/cmake/GTest")
    file(REMOVE "${stamp}")
  endif()
  if(EXISTS "${stamp}")
    return()
  endif()

  message(STATUS "[third-party] building ${cfg} -> ${INTERSTICE_TP_INSTALL}")

  set(cfg_args -G "${CMAKE_GENERATOR}")
  if(CMAKE_GENERATOR_PLATFORM)
    list(APPEND cfg_args -A "${CMAKE_GENERATOR_PLATFORM}")
  endif()
  if(CMAKE_GENERATOR_TOOLSET)
    list(APPEND cfg_args -T "${CMAKE_GENERATOR_TOOLSET}")
  endif()

  # Single-config generators need CMAKE_BUILD_TYPE baked into the configure.
  # For multi-config generators the flag is ignored.
  list(APPEND cfg_args "-DCMAKE_BUILD_TYPE=${cfg}")

  # Inside another project, build with its toolchain and let it find its own
  # packages. An initial cache keeps list values (CMAKE_PREFIX_PATH) intact.
  set(_host_cache "")
  foreach(_var CMAKE_TOOLCHAIN_FILE CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_PREFIX_PATH)
    if(${_var})
      string(APPEND _host_cache "set(${_var} [==[${${_var}}]==] CACHE STRING \"\")\n")
    endif()
  endforeach()
  file(WRITE "${INTERSTICE_TP_BUILD}-host.cmake" "${_host_cache}")
  list(APPEND cfg_args -C "${INTERSTICE_TP_BUILD}-host.cmake")

  execute_process(
    COMMAND ${CMAKE_COMMAND}
      ${cfg_args}
      -S "${PROJECT_SOURCE_DIR}/third_party"
      -B "${INTERSTICE_TP_BUILD}"
      "-DCMAKE_INSTALL_PREFIX=${INTERSTICE_TP_INSTALL}"
      "-DINTERSTICE_BUILD_TESTS=${INTERSTICE_BUILD_TESTS}"
      "-DINTERSTICE_SDL=${INTERSTICE_SDL}"
      "-DINTERSTICE_SYSTEM_SDL=${INTERSTICE_SYSTEM_SDL}"
      "-DINTERSTICE_SYSTEM_HARFBUZZ=${INTERSTICE_SYSTEM_HARFBUZZ}"
      "-DINTERSTICE_SYSTEM_VMA=${INTERSTICE_SYSTEM_VMA}"
    RESULT_VARIABLE _rc
  )
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "[third-party] configure failed (rc=${_rc})")
  endif()

  execute_process(
    COMMAND ${CMAKE_COMMAND}
      --build "${INTERSTICE_TP_BUILD}"
      --config ${cfg}
      --target install
      --parallel
    RESULT_VARIABLE _rc
  )
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "[third-party] ${cfg} build/install failed (rc=${_rc})")
  endif()

  file(WRITE "${stamp}" "${_interstice_tp_signature}")
endfunction()

foreach(_cfg IN LISTS _interstice_tp_configs)
  _interstice_build_third_party_config(${_cfg})
endforeach()

list(PREPEND CMAKE_PREFIX_PATH "${INTERSTICE_TP_INSTALL}")

# RelWithDebInfo and MinSizeRel use the RELEASE runtime (MD), but the cache only
# prebuilds Debug + Release. Without an explicit mapping CMake resolves their
# imported targets to the DEBUG libraries (gmockd, vkbootstrapd, ...), causing
# _ITERATOR_DEBUG_LEVEL / RuntimeLibrary (MDd vs MD) link mismatches. Map both to
# the Release dependency build. Must be set before the find_package() calls so it
# applies to the imported targets they create.
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO "RelWithDebInfo;Release")
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL     "MinSizeRel;Release")

# A target the host already defined is used as it is.
if(INTERSTICE_SDL AND NOT TARGET SDL3::SDL3)
  find_package(SDL3 CONFIG REQUIRED)
endif()

# skribidi's exported config references its dependencies as imported targets
# but doesn't call find_dependency() on them - load them first so the targets
# exist when skribidiConfig.cmake is parsed.
# harfbuzz links Threads::Threads where it uses pthreads, without finding it.
find_package(Threads REQUIRED)
if(NOT TARGET harfbuzz::harfbuzz)
  find_package(harfbuzz CONFIG REQUIRED)
endif()
find_package(SheenBidi CONFIG REQUIRED)
find_package(libunibreak CONFIG REQUIRED)
find_package(budouxc CONFIG REQUIRED)
find_package(skribidi CONFIG REQUIRED)

if(INTERSTICE_SYSTEM_VMA)
  if(NOT TARGET GPUOpen::VulkanMemoryAllocator)
    find_package(VulkanMemoryAllocator CONFIG REQUIRED)
  endif()
  # An alias can't name an alias, which is what VMA's own project defines.
  get_target_property(_vma_target GPUOpen::VulkanMemoryAllocator ALIASED_TARGET)
  if(NOT _vma_target)
    set(_vma_target GPUOpen::VulkanMemoryAllocator)
  endif()
  add_library(vma ALIAS ${_vma_target})
else()
  find_package(vma CONFIG REQUIRED)
endif()
find_package(nanosvg CONFIG REQUIRED)
find_package(vkbootstrap CONFIG REQUIRED)
if(INTERSTICE_BUILD_TESTS)
  find_package(GTest CONFIG REQUIRED)
endif()

# skribidi's install export lives under the `skribidi::` namespace, but
# src/CMakeLists.txt historically linked the plain `skribidi` target.
# Provide an alias so no call sites have to change.
if(TARGET skribidi::skribidi AND NOT TARGET skribidi)
  add_library(skribidi ALIAS skribidi::skribidi)
endif()
