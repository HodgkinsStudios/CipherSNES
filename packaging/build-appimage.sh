#!/usr/bin/env bash
# Create an AppImage from the original Linux X11 frontend.
# linuxdeploy must be installed separately. Run from any working directory.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="${CIPHERSNES_APPIMAGE_WORK:-$root/.appimage-work}"
mkdir -p "$work"
cmake -S "$root" -B "$work/build" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr -DCIPHERSNES_BUILD_X11=ON -DCIPHERSNES_BUILD_GTK=OFF
cmake --build "$work/build" -j "${JOBS:-2}"
DESTDIR="$work/AppDir" cmake --install "$work/build"
# The native picker is a separate executable, so both are installed.
# linuxdeploy scans the directly linked libraries and constructs AppRun.
export ARCH="${ARCH:-x86_64}"
# This frontend loads SDL2 dynamically with dlopen. Explicitly bundle it
# because linuxdeploy cannot discover it from ordinary ELF link dependencies.
sdl_runtime="$(ldconfig -p | awk '/libSDL2-2.0.so.0 \(/ {print $NF; exit}')"
if [[ -z "$sdl_runtime" ]]; then
  echo "SDL2 runtime missing: install libsdl2-2.0-0 before packaging" >&2
  exit 1
fi
linuxdeploy --appdir "$work/AppDir" --library "$sdl_runtime" --output appimage
printf 'AppImage outputs in %s\n' "$PWD"
