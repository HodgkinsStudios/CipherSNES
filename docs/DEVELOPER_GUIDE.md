# CipherSNES — Developer guide

[Back to README](../README.md) · [Platform builds](PLATFORMS.md) · [Contributing](../CONTRIBUTING.md)

CipherSNES is a C++17 SNES/Super Famicom emulator built around a reusable emulation core and platform-specific desktop frontends. This document covers architecture, build targets, verification and maintenance expectations.

## Source layout

| Location | Responsibility |
| --- | --- |
| `include/snes/` and `src/backend/core/` | CPU, PPU, SPC700, DSP, memory/bus, DMA, cartridges, enhancements, save states and system emulation |
| `src/backend/util/` | Backend support utilities |
| `include/CipherSNES/` and `src/core/` | Host-facing core API and helpers |
| `src/frontend_x11.cpp` | Linux X11/XWayland frontend |
| `src/frontend_native_picker.cpp` | Linux native file-picker helper |
| `src/frontend/` | GTK3/SDL2 frontend for Linux, Windows and macOS |
| `tests/portable_smoke.cpp` | Generated ROM/ZIP integration regression |
| `CMakeLists.txt` | Cross-platform native build configuration |
| `Makefile` and `CipherSNES.cbp` | Existing Linux GNU Make and Code::Blocks workflows |
| `packaging/` | Platform application packaging and metadata |
| `.github/workflows/` | Linux source build, cross-platform regression and release automation |
| `assets/` | Application branding, icons and screenshots |

The frontends must remain independent of emulator hardware behavior. They provide presentation, input, host audio and UI services to the shared core.

## Building and testing

**Cross-platform CMake** (requires CMake 3.20+, C++17 compiler and libarchive):

~~~bash
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --parallel 2
ctest --test-dir build-cmake --output-on-failure
~~~

By default, Linux builds the X11 frontend, while Windows and macOS require explicit `-DCIPHERSNES_BUILD_GTK=ON` to build the GTK3/SDL2 frontend. Other options include `CIPHERSNES_BUILD_SHARED=ON` for a shared core and `BUILD_TESTING=OFF` to omit CTest targets. See the [platform guide](PLATFORMS.md) for the exact prerequisites and commands.

**Linux GNU Make and Code::Blocks:**

~~~bash
make -j2 launcher        # Linux X11 launcher and native picker
make -j2 launcher-gtk    # GTK3/SDL2 frontend (requires development dependencies)
make -j2 core            # static and shared core libraries
make -j2 shared          # shared core library
make clean               # remove Make-generated build/, bin/ and lib/
~~~

The Makefile defaults to optimized GCC compilation (`-O3 -flto=auto`). Reduce parallelism on low-memory systems. Build directories and generated binaries should not be committed.

## Emulation and accuracy policy

- Hardware behavior belongs in the core; operating-system interaction belongs in the frontend.
- Prefer documented hardware semantics and reproducible diagnostic evidence over title-specific hacks.
- Keep frontend frame pacing separate from emulated CPU/PPU/APU scheduling and timing.
- Do not classify a diagnostic as PASS solely because the emulator displays a frame, stops execution or survives a timeout.
- For each diagnostic, record the author's actual success/failure condition and classify **PASS**, **FAIL**, **INCONCLUSIVE**, **UNSUPPORTED** or **NOT RUN**.
- Preserve Linux functionality when changing the shared core, and check native Windows/macOS builds when relevant.

## Public regression test

The repository contains `tests/portable_smoke.cpp`. It generates an original minimal SNES cartridge program, loads raw and ZIP-archived forms, executes a short frame sequence and verifies deterministic output.

~~~bash
ctest --test-dir build-cmake --output-on-failure
~~~

This test covers a narrow loading/execution/video integration path. It **does not** prove CPU instruction accuracy, exact PPU rendering, DMA/HDMA behavior, SPC700/DSP fidelity or commercial game compatibility.

The current v0.1 candidate has passed the Linux CMake/X11 build and included portable smoke test locally. Windows, macOS and distributable Linux builds still require a GitHub Actions run. Historical CP70 diagnostics were **not** rerun against this candidate. Evidence and limitations are recorded in [Accuracy and compatibility](ACCURACY_STATUS.md).

## Assets, user data and portability

The GTK frontend stores saves under a GLib-selected user-data location in the `HodgkinsStudios/CipherSNES` namespace. The Linux X11 frontend uses conventional XDG user directories where available. Application settings, local firmware and cartridge data should not be written to the source checkout.

App packaging needs more than a compiled executable: Windows distribution includes GTK resources and DLLs, while macOS application bundles include runtime libraries and resource data. Use the scripts under `packaging/` rather than assuming a raw CMake binary is independently distributable.

Test archive and path handling across all supported operating systems, particularly when changing cartridge loading, filesystem paths or application resource discovery.

## Pull requests and release hygiene

- Make focused changes and explain the expected behavior, implementation and testing evidence.
- Keep ROMs, firmware, proprietary test files, generated binaries, private saves and temporary diagnostics out of the repository.
- The synthetic regression is an **original generated test** and belongs in source control. External diagnostic ROM sets may be used separately when properly authorized.
- Update the changelog and user-facing documentation when features, platform support or externally visible behavior change.
- For a release, validate all intended platforms, package integrity, architectures, dependency bundling and SHA-256 manifests.
- Do not report a source build as an emulator-accuracy pass.

Once v0.1 is verified, attach native packages and release notes at [GitHub Releases](https://github.com/HodgkinsStudios/CipherSNES/releases).
