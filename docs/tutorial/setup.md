# 1. Set up the project

## The folder

Make a folder for your app, and add TachyonUI to it as a Git submodule:

```text
mkdir my_app
cd my_app
git init
git submodule add https://github.com/LogicalError/TachyonUI.git external/TachyonUI
git submodule update --init --recursive
```

`--recursive` fetches TachyonUI's own dependencies (SDL, HarfBuzz and the rest), so the
build needs no network. Git LFS must be installed before this step: the fonts are stored
in LFS.

The app is three files next to `external/`:

```text
my_app/
  CMakeLists.txt
  my_app.h        the panels and the app class
  my_app.cpp      what the panels and the app draw
  main.cpp
  external/TachyonUI/
```

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.22)
project(my_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(external/TachyonUI)

add_executable(my_app main.cpp my_app.cpp my_app.h)
target_link_libraries(my_app PRIVATE TachyonUI::gui_controls)
```

That is all the setup there is. The shaders, the default theme and the fonts are built
into the library, and SDL is linked in, so the program needs no files or DLLs next to it.
As a subproject TachyonUI builds only its libraries, not its examples and tests.

To use an installed TachyonUI instead of a submodule, replace the `add_subdirectory` line
with `find_package(TachyonUI 0.1 CONFIG REQUIRED)`; see
[Adding the library to a build](../integration/build.md).

## Building

```text
cmake -S . -B build
cmake --build build --config Debug
```

On Linux and macOS add `-G "Ninja Multi-Config"` to the first line. The first configure
builds TachyonUI's third-party libraries once, which takes a few minutes.

The build needs the three source files; the next steps write them.

---

[Tutorial](../tutorial.md) · [2. Buttons →](buttons.md)
