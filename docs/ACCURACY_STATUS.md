# Accuracy, verification and compatibility

[Back to README](../README.md) · [Developer guide](DEVELOPER_GUIDE.md)

**CipherSNES v0.1 does not claim complete SNES compatibility or cycle-perfect hardware accuracy.** Build validation, smoke tests and hardware diagnostics are distinct forms of evidence.

## Current v0.1 candidate verification

The public source includes `tests/portable_smoke.cpp`, an original generated-cartridge regression that loads a synthetic SNES ROM directly and from a ZIP archive, executes frames and compares the output.

| Verification | Current candidate status | Scope |
| --- | --- | --- |
| Linux native source compilation | **PASS locally** (November 15, 2026) | C++17 core and X11 frontend build |
| Generated raw/ZIP-ROM regression | **PASS locally** (1/1) | Narrow cartridge-loading and deterministic-frame test |
| Windows x64 build and packaging | **PENDING GitHub Actions** | GTK3/SDL2 plus executable icon and bundled DLLs |
| macOS ARM64 and Intel | **PENDING GitHub Actions** | GTK3/SDL2 app bundle, resources and icons |
| Linux AppImage and Flatpak packages | **PENDING GitHub Actions** | Native package assembly and startup/installation checks |
| Commercial game compatibility and SNES diagnostics | **NOT RERUN for v0.1** | No verified accuracy percentage |

The regression does **not** show that arbitrary games work, audio is accurate, gamepad mappings work across desktop platforms or hardware timing matches a real SNES. Previously published v0.2 CI records are not a v0.1 result.

## Historical diagnostic results: CP70

The most recently documented pre-release diagnostic ledger classified **219 archive entries**, including duplicate entries:

| Historical verdict | Archive entries |
| --- | ---: |
| Author/reference-verified PASS | 113 |
| Test-reported FAIL | 13 |
| Inconclusive — reliable pass/fail condition not established | 93 |
| **Total** | **219** |

These numbers are **historical**. They were recorded before this v0.1 preparation pass and were **not rerun against the current source**. The total is a count of archive entries, **not unique ROM hashes**.

An observed test PASS means only that the documented success condition of that diagnostic was met under its recorded conditions. It does not establish that an entire subsystem is accurate.

Previously reported areas needing further investigation include HDMA/IRQ timing, SPC700/S-DSP diagnostics, SPC timer/reset behavior, an SA-1 version-code check and CPU multiplication timing. Some reported failures appeared in reference emulators as well, making hardware-revision-specific evidence important before drawing conclusions.

## Other historical observations

- **61/61** Peter Lemon author-defined terminal outcomes were recorded in CP70, together with observations for five NMI archive entries and nine distinct known-failing ROMs. These were **not rerun for v0.1**.
- **45 commercial-cartridge entries** received limited cold-boot or graphics smoke checks. These observations do **not** establish full gameplay, save-state reliability, sound quality or general compatibility.
- The historical counts above and v0.1's generated regression should **not be combined** into an overall accuracy percentage.

## Reporting a problem

Please include emulator version/commit SHA, game or diagnostic title, cartridge SHA-256 (instead of the game file), region, hardware variant where relevant, minimal reproduction steps, expected result and observed result. For diagnostics, give the author's actual pass/fail oracle and label results as **PASS**, **FAIL**, **INCONCLUSIVE**, **UNSUPPORTED** or **NOT RUN**.

Do not upload copyrighted cartridge images or Nintendo firmware. Reproducible hardware corrections are preferred to title-specific workarounds.

See [Contributing](../CONTRIBUTING.md) for contributor guidance.
