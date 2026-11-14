#!/usr/bin/env bash
# Builds a Windows setup.exe for Alienorum from Linux.
#
# One-time setup (only the first time, or after deleting vcpkg/):
#   git clone https://github.com/microsoft/vcpkg.git
#   ./vcpkg/bootstrap-vcpkg.sh -disableMetrics
#
# Also required (Ubuntu/Debian): apt install g++-mingw-w64-x86-64 nsis
#
# Usage: package/build_windows_installer.sh
# Output: package/Alienorum-<version>-win64.exe (also left in build-win/)

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

repository_status=$(git status --porcelain=v1 --untracked-files=no --ignore-submodules=untracked)
preflight_errors=0
if [ -n "$repository_status" ]; then
    echo "Installer build stopped: tracked files have uncommitted changes." >&2
    echo "It is important to commit and push all release changes before building an installer." >&2
    echo "Commit the tracked changes and push, then rerun this script." >&2
    printf '%s\n' "$repository_status" >&2
    preflight_errors=$((preflight_errors + 1))
fi

if [ ! -x vcpkg/vcpkg ]; then
    echo "vcpkg is not bootstrapped -- see the setup notes at the top of this script." >&2
    preflight_errors=$((preflight_errors + 1))
fi

VCPKG_TRIPLET=x64-mingw-static

# Every payload entry must exist and come from Git; generated caches and local
# working files cannot enter the installer through directory-wide copies.
while IFS= read -r runtime_file || [ -n "$runtime_file" ]; do
    case "$runtime_file" in
        ''|\#*) continue ;;
    esac
    tracked_source="$runtime_file"
    if [ "$runtime_file" = catalogs/soles_alienorum.dat ]; then
        tracked_source="$runtime_file.gz"
    fi
    if ! git ls-files --error-unmatch -- "$tracked_source" >/dev/null 2>&1; then
        echo "Installer payload is not tracked: $tracked_source" >&2
        preflight_errors=$((preflight_errors + 1))
    fi
    if [ ! -s "$tracked_source" ]; then
        echo "Missing or empty runtime source: $tracked_source" >&2
        preflight_errors=$((preflight_errors + 1))
    fi
done < package/runtime_files.txt

if [ "$preflight_errors" -gt 0 ]; then
    echo "Installer preflight found $preflight_errors issue(s). Resolve all entries above, then rerun this script." >&2
    exit 1
fi

# Install the current tracked stellar catalog, including its abundance metadata.
gzip -dc catalogs/soles_alienorum.dat.gz > catalogs/soles_alienorum.dat.tmp
mv catalogs/soles_alienorum.dat.tmp catalogs/soles_alienorum.dat

export CMAKE_BUILD_PARALLEL_LEVEL=2
export VCPKG_MAX_CONCURRENCY=2

nice -n 10 cmake -B build-win \
    -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$(pwd)/cmake/toolchain-mingw64.cmake" \
    -DVCPKG_TARGET_TRIPLET="$VCPKG_TRIPLET" \
    -DVCPKG_MANIFEST_INSTALL=ON \
    -DVCPKG_HOST_TRIPLET=x64-linux \
    -DCMAKE_BUILD_TYPE=Release

nice -n 10 cmake --build build-win --config Release --parallel 2

(cd build-win && nice -n 10 cpack -G NSIS)

mkdir -p package
mv build-win/Alienorum-*-win64.exe package/ 2>/dev/null || mv build-win/*.exe package/
echo "Installer written to package/"
ls -la package/*.exe
