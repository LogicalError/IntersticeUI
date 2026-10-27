@echo off
rem Builds TachyonUI, its examples and tests on Windows: build.bat [Debug|Release]
rem Needs CMake, Visual Studio 2022 or newer, the Vulkan SDK, and Git LFS for the fonts.
setlocal
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Debug
cd /d "%~dp0"

if not exist third_party\SDL\CMakeLists.txt (
    echo Fetching the submodules...
    git submodule update --init --recursive || exit /b 1
)

cmake --preset windows || exit /b 1
cmake --build build --config %CONFIG% --parallel || exit /b 1

echo.
echo Built. The programs are in bin\%CONFIG% and run from any folder, for example:
echo     bin\%CONFIG%\showcase.exe
echo     bin\%CONFIG%\hello_world.exe
