#!/bin/sh
# Builds TachyonUI, its examples and tests on Linux and macOS: ./build.sh [Debug|Release]
# Needs CMake, Ninja, a C++20 compiler, the Vulkan SDK (or the distribution's Vulkan and
# glslang packages), and Git LFS for the fonts.
set -e
config="${1:-Debug}"
cd "$(dirname "$0")"

if [ ! -f third_party/SDL/CMakeLists.txt ]; then
    echo "Fetching the submodules..."
    git submodule update --init --recursive
fi

cmake --preset ninja
cmake --build build --config "$config" --parallel

echo
echo "Built. The programs are in bin/$config and run from any folder, for example:"
echo "    bin/$config/showcase"
echo "    bin/$config/hello_world"
