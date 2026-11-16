# Contributing to CipherSNES

Thank you for your interest in improving CipherSNES. We welcome bug reports, documentation improvements and focused C++17 contributions.

## Before opening a pull request

1. Review the [README](README.md), [developer guide](docs/DEVELOPER_GUIDE.md) and [platform build instructions](docs/PLATFORMS.md).
2. Create a descriptive branch from the current source.
3. Build the affected frontend and shared emulator core.
4. Run the generated-cartridge integration regression from a configured CMake build:

   ~~~bash
   ctest --test-dir build-cmake --output-on-failure
   ~~~

5. Describe what changed, why it is needed, which platforms were tested and any remaining limitations.

On Linux, GNU Make and Code::Blocks are also supported. Windows and macOS use the GTK3/SDL2 CMake target; consult the platform guide for dependency setup.

## Accuracy and regression expectations

Prefer documented SNES hardware behavior and evidence-backed fixes over title-specific hacks, arbitrary delays or unexplained exceptions. When testing a diagnostic ROM, identify its author's success/failure condition and distinguish **PASS**, **FAIL**, **INCONCLUSIVE**, **UNSUPPORTED** and **NOT RUN**.

The public `tests/portable_smoke.cpp` uses an original generated ROM and is suitable for CI, but it is **not** a substitute for comprehensive CPU, PPU, APU or cartridge compatibility diagnostics.

## Useful bug reports

Include:

- CipherSNES version or commit SHA and operating system.
- Game/diagnostic title, cartridge region and SHA-256 hash.
- Steps to reproduce and expected versus observed behavior.
- Relevant error output, screenshots or recordings when possible.
- For diagnostics, the documented pass/fail condition and supporting evidence.

**Do not attach** commercial ROM dumps, Nintendo firmware, copyrighted diagnostic collections or private user data. A title and SHA-256 are enough to identify the cartridge without distributing it.

## Code and documentation

Keep hardware emulation separate from frontend and operating-system-specific logic. Preserve existing Linux behavior when making shared-core changes, run other native platform CI checks where relevant, and update documentation when public features change.

A successful build does **not** prove hardware accuracy. See [Accuracy and compatibility](docs/ACCURACY_STATUS.md).

The project source uses the [MIT License](LICENSE); third-party dependencies retain their own licenses.
