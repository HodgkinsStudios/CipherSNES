# CipherSNES v0.1 — Platform support and build guide

[Back to README](../README.md) · [Release workflows](../.github/workflows/release-0.1.yml)

**Release status:** The cross-platform v0.1 packaging workflow publishes release artifacts automatically **only when** all build, smoke-test and checksum jobs pass. See [GitHub Actions](https://github.com/HodgkinsStudios/CipherSNES/actions/workflows/release-0.1.yml) for current verification and [GitHub Releases](https://github.com/HodgkinsStudios/CipherSNES/releases) for published assets. An absent release means the CI gate has not yet published.

A successful build or smoke test does not guarantee full compatibility on an end-user system. See [verification](#verification-pending-for-v01) and [limitations](#limitations).

## Platform matrix

| Platform | Architecture | Frontend | Distributed format |
| --- | --- | --- | --- |
| Linux | x86_64 (release packages) | X11/XWayland; optional GTK3/SDL2 | AppImage or Flatpak |
| Windows | x86_64 | GTK3/SDL2 | ZIP with bundled DLLs and launcher |
| macOS | ARM64 / x86_64 | GTK3/SDL2 | ZIP containing application bundle |

The same C++17 emulation core is used on all three operating systems. The original X11 frontend is Linux-specific, whereas the GTK3/SDL2 frontend is cross-platform.

## Build configuration

CMake **3.20+** and a C++17 compiler are required.

| Option | Default | Purpose |
| --- | --- | --- |
| `CIPHERSNES_BUILD_X11` | ON on Linux, OFF elsewhere | Native Linux X11 frontend and file-picker helper |
| `CIPHERSNES_BUILD_GTK` | OFF | GTK3/SDL2 frontend |
| `CIPHERSNES_BUILD_SHARED` | OFF | Optional shared core library |
| `BUILD_TESTING` | ON | CTest synthetic cartridge regression |

Libarchive development files are always required. Installing GTK3/SDL2 headers is necessary when `CIPHERSNES_BUILD_GTK=ON`; the X11 launcher requires X11 headers.

## Linux source build

On Ubuntu:

~~~bash
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config libarchive-dev libx11-dev
git clone https://github.com/HodgkinsStudios/CipherSNES.git
cd CipherSNES
cmake -S . -B build-cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --parallel 2
ctest --test-dir build-cmake --output-on-failure
./build-cmake/CipherSNES
~~~

For the GTK frontend, also install `libgtk-3-dev` and `libsdl2-dev`, then configure with `-DCIPHERSNES_BUILD_GTK=ON`. The resulting executable is `build-cmake/CipherSNES-GTK`. The Linux X11 frontend loads SDL2 at runtime when available (package `libsdl2-2.0-0`); `zenity` can supply an external native file picker.

GNU Make and Code::Blocks are also supported on Linux:

~~~bash
make -j2 launcher core
./RUN_CipherSNES.sh
~~~

## Windows x64 (MSYS2 UCRT64)

Install [MSYS2](https://www.msys2.org/) and open its **UCRT64** shell. Install the compiler and development packages:

~~~bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-libarchive \
  mingw-w64-ucrt-x86_64-gtk3 \
  mingw-w64-ucrt-x86_64-SDL2
~~~

From the repository root, in the same UCRT64 shell:

~~~bash
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCIPHERSNES_BUILD_X11=OFF -DCIPHERSNES_BUILD_GTK=ON
cmake --build build-win --parallel 2
ctest --test-dir build-win --output-on-failure
~~~

The development executable is `build-win/CipherSNES-GTK.exe`. To produce a redistributable ZIP, additionally install `mingw-w64-ucrt-x86_64-ntldd` and run:

~~~bash
bash packaging/build-windows.sh
~~~

This places a bundled application in `release/CipherSNES-v0.1-Windows-x64/`. The batch launcher, GTK loaders, schemas and other dependencies are needed to run the application outside MSYS2.

## macOS (Apple Silicon and Intel)

Install [Homebrew](https://brew.sh/) and the build dependencies:

~~~bash
brew install cmake ninja pkg-config libarchive gtk+3 sdl2
export PKG_CONFIG_PATH="$(brew --prefix libarchive)/lib/pkgconfig:$PKG_CONFIG_PATH"

cmake -S . -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix libarchive)" \
  -DCIPHERSNES_BUILD_GTK=ON
cmake --build build-mac --parallel 2
ctest --test-dir build-mac --output-on-failure
~~~

The CMake application bundle is `build-mac/CipherSNES-GTK.app`. For a redistributable bundle, install `dylibbundler`, then run:

~~~bash
brew install dylibbundler
bash packaging/build-macos.sh
~~~

The script bundles macOS dynamic libraries, resources and prepares an architecture-specific ZIP. The packaging workflow checks native bundle signatures and dependencies after CI runs. macOS binaries are **not Apple-notarized**.

## Linux Flatpak

Install `flatpak` and `flatpak-builder`, and configure the Flathub runtime repository:

~~~bash
flatpak --user remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak --user install flathub org.gnome.Platform//50 org.gnome.Sdk//50
flatpak-builder --user --install --force-clean flatpak-build \
  packaging/io.github.HodgkinsStudios.CipherSNES.yml
flatpak run io.github.HodgkinsStudios.CipherSNES
~~~

The manifest builds dependencies from online source archives. The GitHub Actions release workflow is configured to produce a directly installable `.flatpak` bundle. The application is **not published as a Flathub store entry**.

## Linux AppImage

On an x86_64 Linux host, install the core/X11 build dependencies, the SDL2 runtime and [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy), then run:

~~~bash
bash packaging/build-appimage.sh
~~~

The build uses the native X11 frontend. The release CI packages the application on Ubuntu 22.04; compatibility with other Linux distributions and desktop environments may differ.

## Verification pending for v0.1

The following GitHub Actions checks are **configured, not yet verified for this source revision**:

- [Portability CI](../.github/workflows/portability-0.1.yml): native Linux, Windows x64 and macOS builds plus a generated raw/ZIP cartridge smoke test.
- [Release packaging](../.github/workflows/release-0.1.yml): Windows ZIP; Intel and Apple Silicon macOS `.app` ZIPs; Linux AppImage and Flatpak; combined ZIP with checksums.
- Packaging scripts check bundled GTK dependencies, macOS app layout/signing, AppImage startup and Flatpak installability.
- Desktop icon checks cover the packaged Windows `.ico` and macOS `.icns`, and Linux PNG/PPM assets.

**Run the workflows on GitHub before publishing the new v0.1 release.** Inspect produced artifacts, SHA-256 manifests and native window/About logos. A successful CI build is not a full compatibility test.

## Limitations

These tests do **not** establish full game compatibility, CPU/PPU/APU cycle accuracy, flawless audio playback, working controllers on every end-user device, or clean-host runtime behavior on all operating system versions. Mac builds are not notarized. v0.1 does not publish native ARM Linux or Windows ARM packages.

See [Accuracy and compatibility](ACCURACY_STATUS.md) and the [developer guide](DEVELOPER_GUIDE.md) for interpretation and contribution guidelines.
