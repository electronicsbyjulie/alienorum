# CMake toolchain for cross-compiling alienorum to 64-bit Windows from Linux
# using the system mingw-w64 cross-compiler. Usage:
#
#   cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
#         -DCMAKE_BUILD_TYPE=Release
#
# Chain-load this together with vcpkg's toolchain via VCPKG_CHAINLOAD_TOOLCHAIN_FILE
# (see package/build_windows_installer.sh) to also get vcpkg-built Windows deps.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER   ${TOOLCHAIN_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})

# Search for programs on the host. LIBRARY/INCLUDE/PACKAGE are BOTH (not ONLY): with
# ONLY, CMake re-prefixes every search path (including vcpkg's own absolute install
# path, from CMAKE_PREFIX_PATH) under CMAKE_FIND_ROOT_PATH, which mangles vcpkg's path
# into something that doesn't exist. BOTH still finds vcpkg's static-mingw libraries
# and headers directly, and cross-linking safety isn't really at risk here since the
# host's own Linux libraries are the wrong ABI for the mingw linker regardless.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# Statically link the mingw runtime so the exe carries no extra DLL dependencies
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
