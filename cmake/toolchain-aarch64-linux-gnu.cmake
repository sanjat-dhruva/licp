#
# @file toolchain-aarch64-linux-gnu.cmake
# @brief Cross-compile for the ZSOM-F01 (Zynq UltraScale+ XCZU3EG, Arm Cortex-A53, 64-bit Linux).
#
# Usage:
#   cmake -S . -B build-zsom -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-linux-gnu.cmake \
#         -DCMAKE_SYSROOT=/path/to/zsom/sysroot -DLICP_BUILD_TESTS=OFF
#
# With a Yocto or PetaLinux SDK, source its environment script instead and
# pass its toolchain file; this file is for a plain GNU cross toolchain.
#
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(LICP_CROSS_PREFIX "aarch64-linux-gnu-" CACHE STRING "Cross toolchain prefix")
set(CMAKE_C_COMPILER   ${LICP_CROSS_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${LICP_CROSS_PREFIX}g++)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Cortex-A53 tuning
set(CMAKE_C_FLAGS_INIT   "-mcpu=cortex-a53")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-a53")
