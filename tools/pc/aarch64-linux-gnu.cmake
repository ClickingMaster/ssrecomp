# CMake toolchain for the ARM64 Linux build of the PC all-games program, made on an x86_64
# Linux or WSL machine (Ubuntu: sudo apt install g++-aarch64-linux-gnu). Used by
# tools/pc/build_all.sh linux-arm64.
#
# SSR_A64_FIND_ROOT is a semicolon-separated list of directories that hold the ARM libraries
# and headers (the cross sysroot, the SDL2 built for ARM, and the small stand-in libraries
# the script makes so SDL's video and sound drivers can be compiled). CMake must not pick up
# the machine's own x86_64 libraries.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
if(NOT DEFINED ENV{SSR_A64_FIND_ROOT} OR "$ENV{SSR_A64_FIND_ROOT}" STREQUAL "")
    message(FATAL_ERROR "SSR_A64_FIND_ROOT is not set (tools/pc/build_all.sh sets it)")
endif()
set(CMAKE_FIND_ROOT_PATH $ENV{SSR_A64_FIND_ROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
