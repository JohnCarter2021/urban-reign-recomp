#!/bin/sh
# Build dependencies of the Linux build (docs/LINUX.md): Clang, CMake, Ninja, the X11/OpenGL headers raylib's GLFW
# needs, the Vulkan headers and shaderc (PCSX2's GS on Vulkan compiles its GLSL shaders through shaderc's shared
# library, loaded at run time) and libpng (HD texture packs).
# - No Wayland headers: the window is X11 (XWayland on a Wayland desktop), which is what PCSX2's GS renders into.
# - SDL3 (controllers) is built from source by CMake and finds the controllers in /dev/input.
# - No FFmpeg: the FMVs go through the emulated IPU (URBAN_FFMPEG=OFF on Linux).
# Debian/Ubuntu (apt), Fedora (dnf) and Arch (pacman).
set -e
if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get install -y \
        cmake ninja-build clang lld ccache pkg-config python3 \
        libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxext-dev libxfixes-dev libgl-dev \
        libvulkan-dev vulkan-tools libshaderc-dev libshaderc1 \
        libpng-dev
elif command -v dnf >/dev/null 2>&1; then
    sudo dnf install -y \
        cmake ninja-build clang lld ccache pkgconf-pkg-config python3 \
        libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel libXext-devel libXfixes-devel \
        mesa-libGL-devel \
        vulkan-loader-devel vulkan-tools libshaderc-devel \
        libpng-devel
elif command -v pacman >/dev/null 2>&1; then
    sudo pacman -S --needed \
        cmake ninja clang lld ccache pkgconf python \
        libx11 libxrandr libxinerama libxcursor libxi libxext libxfixes mesa \
        vulkan-icd-loader vulkan-headers vulkan-tools shaderc \
        libpng
else
    echo "Unknown package manager. Install: CMake, Ninja, Clang, LLD, pkg-config, the X11/OpenGL headers," >&2
    echo "the Vulkan loader and headers, shaderc (its shared library) and libpng." >&2
    exit 1
fi
