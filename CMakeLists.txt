cmake_minimum_required(VERSION 3.22)

set(INTERSTICE_VERSION 0.1.0)
project(IntersticeUI VERSION ${INTERSTICE_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakeTargets")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# On by default only when this is the top-level project.
option(INTERSTICE_BUILD_DEMO  "Build the demo application" ${PROJECT_IS_TOP_LEVEL})
option(INTERSTICE_BUILD_TESTS "Build the tests"            ${PROJECT_IS_TOP_LEVEL})

find_package(Vulkan REQUIRED)

include(CompilerOptions)
include(Submodules)
include(ThirdPartyCache)
include(Shaders)

add_subdirectory(src)

# The libraries load the compiled shaders at runtime.
add_dependencies(gui_core Shaders)

# ---- Packaging -------------------------------------------------------------
# `cmake --install <build> --config Debug|Release --prefix <dir>` produces a
# self-contained package: the gui_core / gui_controls libraries, their headers,
# runtime resources (themes, fonts, icons, compiled shaders), and the bundled
# third-party libraries, plus a IntersticeUIConfig.cmake so consumers use
# find_package(IntersticeUI CONFIG). Install both configurations into the same
# prefix to ship Debug and Release side by side.
option(INTERSTICE_INSTALL_THIRD_PARTY "Bundle the prebuilt third-party libraries into the install prefix" ON)

include(CMakePackageConfigHelpers)
set(INTERSTICE_RESOURCE_INSTALL_DIR "share/IntersticeUI/resources")

install(TARGETS gui_core gui_controls
  EXPORT IntersticeUITargets
  ARCHIVE DESTINATION "lib/$<CONFIG>")

install(DIRECTORY src/gui_core src/gui_controls
  DESTINATION include
  FILES_MATCHING PATTERN "*.h")
install(FILES "${PROJECT_BINARY_DIR}/generated/gui_core/gui_version.h"
  DESTINATION include/gui_core)

install(FILES THIRD_PARTY_NOTICES.txt DESTINATION share/IntersticeUI)

install(DIRECTORY
    workdirectory/themes
    workdirectory/fonts
    workdirectory/icons
    workdirectory/shaders_compiled
  DESTINATION "${INTERSTICE_RESOURCE_INSTALL_DIR}"
  PATTERN demo.css EXCLUDE)

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

if(INTERSTICE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
