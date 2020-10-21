#!/bin/bash

set -e

shopt -s extglob

if [ -z "$1" ] || [ -z "$2" ]; then
  echo "Usage: $0 version destdir [--native] [--no-package] [--dev-build]"
  exit 1
fi

VKD3D_VERSION="$1"
VKD3D_SRC_DIR=$(dirname "$(readlink -f "$0")")
VKD3D_BUILD_DIR=$(realpath "$2")"/vkd3d-$VKD3D_VERSION"
VKD3D_ARCHIVE_PATH=$(realpath "$2")"/vkd3d-$VKD3D_VERSION.tar.zst"

if [ -e "$VKD3D_BUILD_DIR" ]; then
  echo "Build directory $VKD3D_BUILD_DIR already exists"
  exit 1
fi

shift 2

opt_nopackage=0
opt_devbuild=0
opt_native=0

while [ $# -gt 0 ]; do
  case "$1" in
  "--native")
    opt_native=1
    ;;
  "--no-package")
    opt_nopackage=1
    ;;
  "--dev-build")
    opt_nopackage=1
    opt_devbuild=1
    ;;
  *)
    echo "Unrecognized option: $1" >&2
    exit 1
  esac
  shift
done

function build_arch {
  cd "$VKD3D_SRC_DIR"

  # shellcheck disable=SC2086
  meson $2                           \
        --buildtype "release"        \
        --prefix "$VKD3D_BUILD_DIR"  \
        --strip                      \
        --bindir "x$1"               \
        --libdir "x$1"               \
        "$VKD3D_BUILD_DIR/build.$1"

  cd "$VKD3D_BUILD_DIR/build.$1"
  ninja install

  if [ $opt_devbuild -eq 0 ]; then
    if [ $opt_native -eq 0 ]; then
        # get rid of some useless .a files
        rm "$VKD3D_BUILD_DIR/x$1/"*.!(dll)
        # get rid of vkd3d-utils.dll
        rm "$VKD3D_BUILD_DIR/x$1/libvkd3d-utils.dll"
    fi
    rm -R "$VKD3D_BUILD_DIR/build.$1"
  fi
}

function build_script {
  cp "$VKD3D_SRC_DIR/setup_vkd3d.sh" "$VKD3D_BUILD_DIR/setup_vkd3d.sh"
  chmod +x "$VKD3D_BUILD_DIR/setup_vkd3d.sh"
}

function package {
  cd "$VKD3D_BUILD_DIR/.."
  tar -caf "$VKD3D_ARCHIVE_PATH" "vkd3d-$VKD3D_VERSION"
  rm -R "vkd3d-$VKD3D_VERSION"
}

if [ $opt_native -eq 0 ]; then
  build_arch 64 "--cross-file build-win64.txt -Denable_standalone_d3d12=True"
  build_arch 86 "--cross-file build-win32.txt -Denable_standalone_d3d12=True"
  build_script
else
  build_arch 64
  build_arch 86 "--cross-file x86-linux-gnu"
fi

if [ $opt_nopackage -eq 0 ]; then
  package
fi
