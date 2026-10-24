cmake_minimum_required(VERSION 3.22)

set(INTERSTICE_VERSION 0.1.0)
project(IntersticeUI VERSION ${INTERSTICE_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Built on its own, programs go to bin/ and the resources load from workdirectory/.
# Inside another project nothing is written to the source tree: the host decides
# where programs go, and the resources (compiled shaders, copies of the fonts and
# themes) are put together in the build tree.
if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
  set(INTERSTICE_BUILD_RESOURCE_DIR "${PROJECT_SOURCE_DIR}/workdirectory")
else()
  set(INTERSTICE_BUILD_RESOURCE_DIR "${PROJECT_BINARY_DIR}/resources")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakeTargets")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# On by default only when this is the top-level project.
option(INTERSTICE_BUILD_DEMO  "Build the demo application" ${PROJECT_IS_TOP_LEVEL})
option(INTERSTICE_BUILD_TESTS "Build the tests"            ${PROJECT_IS_TOP_LEVEL})
option(INTERSTICE_BUILD_EXAMPLES "Build the examples"      ${PROJECT_IS_TOP_LEVEL})
# Off: no SDL at all; the host passes its own window backend (SetWindowBackend).
option(INTERSTICE_SDL "Build the SDL window backend and clipboard" ON)

# Use the host's copy of a dependency instead of building the bundled one: a
# target the host already defined (SDL3::SDL3, harfbuzz::harfbuzz,
# GPUOpen::VulkanMemoryAllocator), or else find_package().
option(INTERSTICE_SYSTEM_SDL      "Use the host's SDL3 instead of the bundled one"      OFF)
option(INTERSTICE_SYSTEM_HARFBUZZ "Use the host's harfbuzz instead of the bundled one"  OFF)
option(INTERSTICE_SYSTEM_VMA      "Use the host's VulkanMemoryAllocator instead of the bundled one" OFF)

# The Noto fallback fonts (other scripts, emoji, symbols; about 87 MB) are optional:
# an app loads them with InitializeOptions::fallbackFonts. Off: resources and the
# package carry only the built-in fonts.
option(INTERSTICE_FALLBACK_FONTS "Ship the optional Noto fallback fonts with the resources" OFF)

find_package(Vulkan REQUIRED)

include(CompilerOptions)
include(Submodules)
include(ThirdPartyCache)
include(Shaders)

add_subdirectory(src)
if(INTERSTICE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# The libraries load the compiled shaders at runtime.
add_dependencies(gui_core interstice_shaders)
if(NOT PROJECT_IS_TOP_LEVEL)
  add_dependencies(gui_core interstice_resources)
endif()

# ---- Packaging -------------------------------------------------------------
# `cmake --install <build> --config Debug|Release --prefix <dir>` produces a
# self-contained package: the gui_core / gui_controls libraries, their headers,
# runtime resources (themes, fonts, icons, compiled shaders), and the bundled
# third-party libraries, plus a IntersticeUIConfig.cmake so consumers use
# find_package(IntersticeUI CONFIG). Install both configurations into the same
# prefix to ship Debug and Release side by side.
# Inside another project the host decides what to install; its own targets (a VMA
# it builds itself, say) can't be part of this package's export.
option(INTERSTICE_INSTALL "Generate the install rules and the package" ${PROJECT_IS_TOP_LEVEL})
if(INTERSTICE_INSTALL)
  option(INTERSTICE_INSTALL_THIRD_PARTY "Bundle the prebuilt third-party libraries into the install prefix" ON)

  include(CMakePackageConfigHelpers)
  set(INTERSTICE_RESOURCE_INSTALL_DIR "share/IntersticeUI/resources")

  install(TARGETS gui_core gui_controls
    EXPORT IntersticeUITargets
    ARCHIVE DESTINATION "lib/$<CONFIG>")

  # Public headers only: *_internal.h and internal/ folders are the library's own.
  set(_interstice_header_excludes PATTERN "*_internal.h" EXCLUDE PATTERN "internal" EXCLUDE)
  if(NOT INTERSTICE_SDL)
    list(APPEND _interstice_header_excludes PATTERN "sdl" EXCLUDE)
  endif()
  install(DIRECTORY src/gui_core src/gui_controls
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
    ${_interstice_header_excludes})
  install(FILES "${PROJECT_BINARY_DIR}/generated/gui_core/gui_version.h"
    DESTINATION include/gui_core)

  install(FILES LICENSE THIRD_PARTY_NOTICES.txt DESTINATION share/IntersticeUI)

  set(_interstice_resource_excludes PATTERN demo.css EXCLUDE)
  if(NOT INTERSTICE_FALLBACK_FONTS)
    list(APPEND _interstice_resource_excludes PATTERN Noto-otf EXCLUDE)
  endif()
  install(DIRECTORY
      workdirectory/themes
      workdirectory/fonts
      workdirectory/icons
      "${INTERSTICE_BUILD_RESOURCE_DIR}/shaders_compiled"
    DESTINATION "${INTERSTICE_RESOURCE_INSTALL_DIR}"
    ${_interstice_resource_excludes})

  if(INTERSTICE_INSTALL_THIRD_PARTY)
    install(DIRECTORY "${INTERSTICE_TP_INSTALL}/"
      DESTINATION .
      PATTERN ".stamp-*" EXCLUDE
      PATTERN "gtest*" EXCLUDE
      PATTERN "gmock*" EXCLUDE
      PATTERN "GTest" EXCLUDE)
  endif()

  install(EXPORT IntersticeUITargets
    FILE IntersticeUITargets.cmake
    NAMESPACE IntersticeUI::
    DESTINATION lib/cmake/IntersticeUI)

  configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/IntersticeUIConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/IntersticeUIConfig.cmake"
    INSTALL_DESTINATION lib/cmake/IntersticeUI
    PATH_VARS INTERSTICE_RESOURCE_INSTALL_DIR)

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/IntersticeUIConfigVersion.cmake"
    VERSION ${INTERSTICE_VERSION}
    COMPATIBILITY SameMinorVersion)

  install(FILES
      "${CMAKE_CURRENT_BINARY_DIR}/IntersticeUIConfig.cmake"
      "${CMAKE_CURRENT_BINARY_DIR}/IntersticeUIConfigVersion.cmake"
    DESTINATION lib/cmake/IntersticeUI)
endif()

if(INTERSTICE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
