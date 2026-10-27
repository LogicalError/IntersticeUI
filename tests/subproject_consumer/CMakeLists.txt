# Standalone project that adds TachyonUI with add_subdirectory, the way a host
# embeds it. Not part of the main build: it proves the subproject build works and
# writes nothing into the TachyonUI source tree. Runs the package consumer's
# program against the build tree's resources.
#
#   cmake -S tests/subproject_consumer -B <consumer build>
#   cmake --build <consumer build> --config Release
#
# CONSUMER_HOST_DEPENDENCIES=ON brings the host's own SDL3, harfbuzz and VMA
# instead: SDL3 and VMA as targets the host defines, harfbuzz through
# find_package. CMAKE_PREFIX_PATH must then find SDL3 and harfbuzz.
cmake_minimum_required(VERSION 3.22)
project(tachyon_ui_subproject_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CONSUMER_HOST_DEPENDENCIES "Use the host's SDL3, harfbuzz and VMA" OFF)
if(CONSUMER_HOST_DEPENDENCIES)
  find_package(SDL3 CONFIG REQUIRED)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/vma" vma)
  set(TACHYON_SYSTEM_SDL      ON)
  set(TACHYON_SYSTEM_HARFBUZZ ON)
  set(TACHYON_SYSTEM_VMA      ON)
endif()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../.." TachyonUI)

add_executable(subproject_consumer ../package_consumer/main.cpp)
target_link_libraries(subproject_consumer PRIVATE TachyonUI::gui_controls)

add_custom_command(TARGET subproject_consumer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_RUNTIME_DLLS:subproject_consumer> $<TARGET_FILE_DIR:subproject_consumer>
  COMMAND_EXPAND_LISTS)
