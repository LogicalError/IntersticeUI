# Standalone consumer of the installed TachyonUI package. Not part of the
# main build: it proves that `find_package(TachyonUI)` yields working targets
# without any knowledge of the TachyonUI source or build tree.
#
#   cmake --install <TachyonUI build> --config Debug   --prefix <prefix>
#   cmake --install <TachyonUI build> --config Release --prefix <prefix>
#   cmake -S tests/package_consumer -B <consumer build> -DCMAKE_PREFIX_PATH=<prefix>
#   cmake --build <consumer build> --config Release
cmake_minimum_required(VERSION 3.22)
project(tachyon_ui_package_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(TachyonUI 0.1 CONFIG REQUIRED)

add_executable(package_consumer main.cpp)
target_link_libraries(package_consumer PRIVATE TachyonUI::gui_controls)

# Windows finds DLLs next to the executable; elsewhere the build RPATH finds SDL3.
if(WIN32)
  add_custom_command(TARGET package_consumer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:package_consumer> $<TARGET_FILE_DIR:package_consumer>
    COMMAND_EXPAND_LISTS)
endif()
