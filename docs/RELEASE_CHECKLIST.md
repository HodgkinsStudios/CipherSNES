# CipherSNES v0.1 — release checklist

This source tree is prepared as a **v0.1 release candidate**, not a verified public release. Do not label native platforms verified until their workflows and manual checks pass.

## Before uploading

- [ ] Confirm `assets/CipherSNES_icon.png` is the approved neon badge shown in the README.
- [ ] Confirm `assets/CipherSNES_icon_256.png`, `.ico`, `.icns` and `CipherSNES_about.ppm` are present.
- [ ] Run `python3 packaging/verify-branding.py`.
- [ ] Build Linux and run `ctest --output-on-failure`.
- [ ] Confirm the old About screenshot and `v0.2` publishing workflow are absent.

## Publish source to GitHub `main`

For the **existing repository**, clone the latest main, copy in the contents of this ZIP excluding `.git`, and **remove the obsolete files** listed below before committing. Compare differences with `git status` to avoid overwriting independent changes. Commit and `git push origin main`.

For a **new empty replacement repository** (after you create `HodgkinsStudios/CipherSNES` on GitHub), extract the source ZIP, open a terminal in the extracted project root, and run:

```bash
git init -b main
git add -A
git commit -m "Prepare CipherSNES v0.1 cross-platform release with unified branding"
git remote add origin https://github.com/HodgkinsStudios/CipherSNES.git
git push -u origin main
```

Git requires a configured user name/email and authenticated GitHub push permission. Do **not** force-push to a pre-existing non-empty repository.

If updating existing main, delete these old paths:

- `.github/workflows/portability-0.2.yml`
- `.github/workflows/release-0.2.yml`
- `.github/workflows/publish-v0.2.yml` (hardcoded previous CI run IDs; intentionally removed)
- `docs/PLATFORMS_0.2.md`
- `assets/CipherSNES_about_screen.png` (obsolete image of the previous logo)

## After pushing

- [ ] Confirm the README shows the new badge on GitHub.
- [ ] Run the **Linux source build** workflow.
- [ ] Run the **v0.1 desktop portability (development)** workflow and inspect Linux, Windows and macOS native build/test results.
- [ ] Run **0.1 release packages** and download the Windows x64, macOS ARM64/Intel, AppImage, Flatpak and combined release artifacts.
- [ ] On **Windows**, verify Explorer's EXE icon, taskbar icon, GTK window icon and Help/About image.
- [ ] On **macOS**, verify Finder/Dock icon, app bundle `CFBundleIconFile`, window icon and Help/About image on Apple Silicon and Intel.
- [ ] On **Linux**, verify X11 window/About icon, GTK Help/About icon, desktop file and installed Flatpak/AppImage branding.
- [ ] Confirm input, file dialogs, audio and ROM loading on all target operating systems; check package checksums.
- [ ] Record any failures and rerun CI before release. Update the README status text from *candidate* to the verified outcome and attach verified packages to the new `v0.1` GitHub Release.

No commercial cartridge ROMs or firmware should be committed or distributed.
