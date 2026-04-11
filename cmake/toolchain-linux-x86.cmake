# CMake toolchain file for building 32-bit (i386) binaries on Linux x86_64.
# Requires multilib gcc: apt install gcc-multilib g++-multilib
#
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-linux-x86.cmake ...

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i686)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32" CACHE STRING "")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32" CACHE STRING "")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -m32" CACHE STRING "")
