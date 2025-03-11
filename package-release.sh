#!/usr/bin/env bash

# Script to build and package vkd3d-proton
# Requires: meson, ninja, tar, zstd

set -e  # Exit on any error
shopt -s extglob  # Enable extended globbing

# Print usage information
usage() {
    echo "Usage: $0 version destdir [--native] [--no-package] [--dev-build] [--debug]"
    echo "  version:    Version string for vkd3d-proton"
    echo "  destdir:    Destination directory for build output"
    echo "  --native:   Build native Linux version instead of Windows"
    echo "  --no-package: Skip packaging step"
    echo "  --dev-build: Keep debug symbols and build directories"
    echo "  --debug:    Build with debug configuration"
    exit 1
}

# Check required arguments
[ $# -lt 2 ] && usage
[ -z "$1" ] || [ -z "$2" ] && usage

# Set up variables
VKD3D_VERSION="$1"
VKD3D_SRC_DIR="$(dirname "$(readlink -f "$0")")"
VKD3D_BUILD_DIR="$(realpath "$2")/vkd3d-proton-$VKD3D_VERSION"
VKD3D_ARCHIVE_PATH="$(realpath "$2")/vkd3d-proton-$VKD3D_VERSION.tar.zst"

# Check if build directory already exists
if [ -e "$VKD3D_BUILD_DIR" ]; then
    echo "Error: Build directory $VKD3D_BUILD_DIR already exists"
    exit 1
fi

# Parse optional arguments
shift 2
opt_nopackage=0
opt_devbuild=0
opt_native=0
opt_buildtype="release"
opt_strip="--strip"

while [ $# -gt 0 ]; do
    case "$1" in
        --native)    opt_native=1 ;;
        --no-package) opt_nopackage=1 ;;
        --dev-build) opt_strip=""; opt_nopackage=1; opt_devbuild=1 ;;
        --debug)     opt_buildtype="debug" ;;
        *)           echo "Error: Unrecognized option: $1" >&2; exit 1 ;;
    esac
    shift
done

# Build for specific architecture
build_arch() {
    local arch="$1"
    shift

    echo "Building for x${arch}..."
    cd "$VKD3D_SRC_DIR" || exit 1

    meson setup "$@" \
        --buildtype "${opt_buildtype}" \
        --prefix "$VKD3D_BUILD_DIR" \
        $opt_strip \
        --bindir "x${arch}" \
        --libdir "x${arch}" \
        "$VKD3D_BUILD_DIR/build.${arch}" || {
            echo "Error: Meson setup failed for x${arch}" >&2
            exit 1
        }

    cd "$VKD3D_BUILD_DIR/build.${arch}" || exit 1
    ninja install || {
        echo "Error: Build failed for x${arch}" >&2
        exit 1
    }

    # Cleanup unless in dev-build mode
    if [ $opt_devbuild -eq 0 ]; then
        if [ $opt_native -eq 0 ]; then
            rm -f "$VKD3D_BUILD_DIR/x${arch}/"*.!(dll)
        fi
        rm -rf "$VKD3D_BUILD_DIR/build.${arch}"
    fi
}

# Create setup script
build_script() {
    echo "Creating setup script..."
    cp "$VKD3D_SRC_DIR/setup_vkd3d_proton.sh" "$VKD3D_BUILD_DIR/setup_vkd3d_proton.sh" || {
        echo "Error: Failed to copy setup script" >&2
        exit 1
    }
    chmod +x "$VKD3D_BUILD_DIR/setup_vkd3d_proton.sh"
}

# Package the build
package() {
    echo "Packaging build..."
    cd "$VKD3D_BUILD_DIR/.." || exit 1
    tar -caf "$VKD3D_ARCHIVE_PATH" "vkd3d-proton-$VKD3D_VERSION" || {
        echo "Error: Failed to create archive" >&2
        exit 1
    }
    rm -rf "vkd3d-proton-$VKD3D_VERSION"
}

# Main build process
main() {
    mkdir -p "$VKD3D_BUILD_DIR" || {
        echo "Error: Failed to create build directory" >&2
        exit 1
    }

    if [ $opt_native -eq 0 ]; then
        # Windows cross-compilation
        build_arch 64 --cross-file build-win64.txt
        build_arch 86 --cross-file build-win32.txt
        build_script
    else
        # Native Linux compilation
        build_arch 64
        CC="gcc -m32" \
        CXX="g++ -m32" \
        PKG_CONFIG_PATH="/usr/lib32/pkgconfig:/usr/lib/i386-linux-gnu/pkgconfig:/usr/lib/pkgconfig" \
        build_arch 86
    fi

    if [ $opt_nopackage -eq 0 ]; then
        package
    fi

    echo "Build completed successfully"
}

# Execute main function
main
