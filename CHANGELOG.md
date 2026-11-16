# Changelog

## v0.1 — fresh release candidate (November 15, 2026)

- Prepared a single clean cross-platform v0.1 source tree using the existing emulator core.
- Replaced the legacy project logo with the supplied CipherSNES neon badge everywhere: README, Linux X11 About/window icon, GTK3 About/window icon, application metadata, Windows EXE and macOS `.app` icon.
- Added native multi-resolution `.ico` and `.icns` icons, and regenerated X11's dependency-free PPM derivative from the approved source image.
- Retained the Linux X11 and GTK3/SDL2 frontend options and platform packaging scripts.
- Set program, package and release-workflow versions to 0.1; removed the previously pinned v0.2 automatic publisher.
- Removed the outdated previous-logo About screenshot pending a new verified capture.

**Validation:** current v0.1 GitHub Actions and real-device test outcomes must be recorded after they run. A passing synthetic ROM smoke test alone does not establish hardware accuracy.

See [Accuracy and compatibility](docs/ACCURACY_STATUS.md) for the limitations of the earlier historical diagnostic baseline.
