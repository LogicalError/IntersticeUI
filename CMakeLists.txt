cmake_minimum_required(VERSION 3.22)
# Libraries listed twice on a link line (a static library's private dependency that a
# program also links) appear once where the linker doesn't need them repeated; Apple's
# linker warns about the duplicates otherwise. CMake 3.29 and newer.
if(POLICY CMP0156)
  cmake_policy(SET CMP0156 NEW)
endif()

set(TACHYON_VERSION 0.1.0)
project(TachyonUI VERSION ${TACHYON_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# vcpkg's Visual Studio integration links every library vcpkg has installed into every
# project. With an SDL3 among them there are two SDLs in one process, and each SDL call
# goes to either. Off, unless the build uses vcpkg's toolchain file.
if(CMAKE_GENERATOR MATCHES "Visual Studio" AND NOT VCPKG_TOOLCHAIN)
  list(APPEND CMAKE_VS_GLOBALS "VcpkgEnabled=false")
endif()

# Built on its own, programs go to bin/ and the resources load from workdirectory/.
# Inside another project nothing is written to the source tree: the host decides
# where programs go, and the resources (compiled shaders, copies of the fonts and
# themes) are put together in the build tree.
if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
  set(TACHYON_BUILD_RESOURCE_DIR "${PROJECT_SOURCE_DIR}/workdirectory")
else()
  set(TACHYON_BUILD_RESOURCE_DIR "${PROJECT_BINARY_DIR}/resources")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakeTargets")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# On by default only when this is the top-level project.
option(TACHYON_BUILD_SHOWCASE "Build the showcase application" ${PROJECT_IS_TOP_LEVEL})
option(TACHYON_BUILD_TESTS    "Build the tests"                ${PROJECT_IS_TOP_LEVEL})
option(TACHYON_BUILD_EXAMPLES "Build the small examples"       ${PROJECT_IS_TOP_LEVEL})
# Off: no SDL at all; the host passes its own window backend (SetWindowBackend).
option(TACHYON_SDL "Build the SDL window backend and clipboard" ON)

# Use the host's copy of a dependency instead of building the bundled one: a
# target the host already defined (SDL3::SDL3, harfbuzz::harfbuzz,
# GPUOpen::VulkanMemoryAllocator), or else find_package().
option(TACHYON_SYSTEM_SDL      "Use the host's SDL3 instead of the bundled one"      OFF)
option(TACHYON_SYSTEM_HARFBUZZ "Use the host's harfbuzz instead of the bundled one"  OFF)
option(TACHYON_SYSTEM_VMA      "Use the host's VulkanMemoryAllocator instead of the bundled one" OFF)

# The Noto fallback fonts (other scripts, emoji, symbols; about 87 MB) are optional:
# an app loads them with InitializeOptions::fallbackFonts. Off: resources and the
# package carry only the built-in fonts.
option(TACHYON_FALLBACK_FONTS "Ship the optional Noto fallback fonts with the resources" OFF)

find_package(Vulkan REQUIRED)

include(CompilerOptions)
include(Submodules)
include(ThirdPartyCache)
include(Shaders)

add_subdirectory(src)
if(TACHYON_BUILD_EXAMPLES OR TACHYON_BUILD_SHOWCASE)
  add_subdirectory(examples)
endif()
# Visual Studio runs the showcase on F5, not ALL_BUILD.
if(PROJECT_IS_TOP_LEVEL AND TARGET showcase)
  set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT showcase)
endif()

# The libraries load the compiled shaders at runtime.
add_dependencies(gui_core tachyon_shaders)
if(NOT PROJECT_IS_TOP_LEVEL)
  add_dependencies(gui_core tachyon_resources)
endif()

# ---- Packaging -------------------------------------------------------------
# `cmake --install <build> --config Debug|Release --prefix <dir>` produces a
# self-contained package: the gui_core / gui_controls libraries, their headers,
# runtime resources (themes, fonts, icons, compiled shaders), and the bundled
# third-party libraries, plus a TachyonUIConfig.cmake so consumers use
# find_package(TachyonUI CONFIG). Install both configurations into the same
# prefix to ship Debug and Release side by side.
# Inside another project the host decides what to install; its own targets (a VMA
# it builds itself, say) can't be part of this package's export.
option(TACHYON_INSTALL "Generate the install rules and the package" ${PROJECT_IS_TOP_LEVEL})
if(TACHYON_INSTALL)
  option(TACHYON_INSTALL_THIRD_PARTY "Bundle the prebuilt third-party libraries into the install prefix" ON)

  include(CMakePackageConfigHelpers)
  set(TACHYON_RESOURCE_INSTALL_DIR "share/TachyonUI/resources")

  install(TARGETS gui_core gui_controls
    EXPORT TachyonUITargets
    ARCHIVE DESTINATION "lib/$<CONFIG>")

  # Public headers only: *_internal.h and internal/ folders are the library's own.
  set(_tachyon_header_excludes PATTERN "*_internal.h" EXCLUDE PATTERN "internal" EXCLUDE)
  if(NOT TACHYON_SDL)
    list(APPEND _tachyon_header_excludes PATTERN "sdl" EXCLUDE)
  endif()
  install(DIRECTORY src/gui_core src/gui_controls
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
    ${_tachyon_header_excludes})
  install(FILES "${PROJECT_BINARY_DIR}/generated/gui_core/gui_version.h"
    DESTINATION include/gui_core)

  install(FILES LICENSE THIRD_PARTY_NOTICES.txt DESTINATION share/TachyonUI)

  set(_tachyon_resource_excludes PATTERN showcase.css EXCLUDE)
  if(NOT TACHYON_FALLBACK_FONTS)
    list(APPEND _tachyon_resource_excludes PATTERN Noto-otf EXCLUDE)
  endif()
  install(DIRECTORY
      workdirectory/themes
      workdirectory/fonts
      workdirectory/icons
      "${TACHYON_BUILD_RESOURCE_DIR}/shaders_compiled"
    DESTINATION "${TACHYON_RESOURCE_INSTALL_DIR}"
    ${_tachyon_resource_excludes})

  if(TACHYON_INSTALL_THIRD_PARTY)
    install(DIRECTORY "${TACHYON_TP_INSTALL}/"
      DESTINATION .
      PATTERN ".stamp-*" EXCLUDE
      PATTERN "gtest*" EXCLUDE
      PATTERN "gmock*" EXCLUDE
      PATTERN "GTest" EXCLUDE)
  endif()

  install(EXPORT TachyonUITargets
    FILE TachyonUITargets.cmake
    NAMESPACE TachyonUI::
    DESTINATION lib/cmake/TachyonUI)

  configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/TachyonUIConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/TachyonUIConfig.cmake"
    INSTALL_DESTINATION lib/cmake/TachyonUI
    PATH_VARS TACHYON_RESOURCE_INSTALL_DIR)

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/TachyonUIConfigVersion.cmake"
    VERSION ${TACHYON_VERSION}
    COMPATIBILITY SameMinorVersion)

  install(FILES
      "${CMAKE_CURRENT_BINARY_DIR}/TachyonUIConfig.cmake"
      "${CMAKE_CURRENT_BINARY_DIR}/TachyonUIConfigVersion.cmake"
    DESTINATION lib/cmake/TachyonUI)
endif()

if(TACHYON_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# The API reference, where Doxygen is installed: cmake --build build --target tachyon_docs
# writes it to build/docs/html. The guides in docs/ are its pages.
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND AND PROJECT_IS_TOP_LEVEL)
  add_custom_target(tachyon_docs
    COMMAND "${CMAKE_COMMAND}" -E env "DOXYGEN_OUTPUT=${PROJECT_BINARY_DIR}/docs" "TACHYON_VERSION=${TACHYON_VERSION}"
            "${DOXYGEN_EXECUTABLE}" docs/Doxyfile
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    SOURCES docs/Doxyfile
    COMMENT "Building the API reference"
    VERBATIM)
  set_target_properties(tachyon_docs PROPERTIES FOLDER "CMakeTargets")
endif()
