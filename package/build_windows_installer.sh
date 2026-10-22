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

if [ ! -x vcpkg/vcpkg ]; then
    echo "vcpkg is not bootstrapped -- see the setup notes at the top of this script." >&2
    exit 1
fi

VCPKG_TRIPLET=x64-mingw-static

# Install the current tracked stellar catalog, including its abundance metadata.
gzip -dc catalogs/soles_alienorum.dat.gz > catalogs/soles_alienorum.dat.tmp
mv catalogs/soles_alienorum.dat.tmp catalogs/soles_alienorum.dat

# Only the generated model ships; source catalogs are runtime downloads.
if [ ! -s catalogs/NASA/classification_model.json ]; then
    echo "Missing classification model." >&2
    exit 1
fi

# The fixed host supplement is installed by CMake and must ship with every release.
if [ ! -s catalogs/exoplanet_host_positions.dat ]; then
    echo "Missing catalog: catalogs/exoplanet_host_positions.dat" >&2
    exit 1
fi
if [ ! -s catalogs/galaxy_properties.json ]; then
    echo "Missing catalog: catalogs/galaxy_properties.json" >&2
    exit 1
fi
if [ ! -s catalogs/WD_colors.dat ]; then
    echo "Missing catalog: catalogs/WD_colors.dat" >&2
    exit 1
fi
if [ ! -s catalogs/WD_hydrogen_grid.dat ]; then
    echo "Missing catalog: catalogs/WD_hydrogen_grid.dat" >&2
    exit 1
fi
if [ ! -s catalogs/WD_limb_profiles.dat ]; then
    echo "Missing catalog: catalogs/WD_limb_profiles.dat" >&2
    exit 1
fi

# Starter texture set for the installer: exactly the files git already tracks under
# maps/ (i.e. the bodies whose planets.json entry has no live download URL, or has one
# commented out with '#'). Regenerated every run so it can never silently go stale or
# missing -- see the discussion of this in the project's chat history for why these
# specific files and not others.
rm -rf package/release-maps
mkdir -p package/release-maps
git ls-files maps/ | while read -r f; do cp "$f" package/release-maps/; done

# Same idea for assets/: only ship what's git-tracked, not whatever else happens to be
# sitting in the working directory locally (draft mascot images, .xcf source files,
# one-off renders, unused fonts, orphaned theme presets, etc).
rm -rf package/release-assets
mkdir -p package/release-assets
git ls-files assets/ | while read -r f; do
    mkdir -p "package/release-assets/$(dirname "${f#assets/}")"
    cp "$f" "package/release-assets/${f#assets/}"
done

# Same idea for galaxies/: ship what's git-tracked under galaxies/ (face-on and internal
# maps for galaxy rendering), omitting uncommitted scans and raw images.
rm -rf package/release-galaxies
mkdir -p package/release-galaxies
git ls-files galaxies/ | while read -r f; do
    mkdir -p "package/release-galaxies/$(dirname "${f#galaxies/}")"
    cp "$f" "package/release-galaxies/${f#galaxies/}"
done

cmake -B build-win \
    -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$(pwd)/cmake/toolchain-mingw64.cmake" \
    -DVCPKG_TARGET_TRIPLET="$VCPKG_TRIPLET" \
    -DVCPKG_HOST_TRIPLET=x64-linux \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build-win --config Release -j"$(nproc)"

(cd build-win && cpack -G NSIS)

mkdir -p package
mv build-win/Alienorum-*-win64.exe package/ 2>/dev/null || mv build-win/*.exe package/
echo "Installer written to package/"
ls -la package/*.exe
