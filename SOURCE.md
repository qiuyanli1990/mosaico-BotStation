# Source provenance

This repository starts from a source-only snapshot of development commit
`f97c0421f3178a4a6a39b0bf6787c6123e7dd60b`, titled
`feat: add v3 cute UI, brightness controls and battery status`.
The development checkout was on branch `v4` when this snapshot was exported.

Firmware source, tests, application configuration and existing dependency
versions are retained. Publication preparation replaces the setup documentation
and ignore rules, adds an independent environment bootstrap, changes the build
wrapper to use project-local tools, and pins registry component versions for
first-configuration reproducibility. The two existing third-party integration
patches are reproduced by bootstrap.

The public repository has its own history starting on `main`. Original local
history, release-management scripts, weekly reports, device logs, real local
configuration, credentials, downloaded environments and generated firmware are
excluded. The v2/v3 UI design documents and fixture previews are retained.

The application version remains `0.1.0`. The `v3` UI identifiers describe the
existing touchscreen layout; they do not refer to a public Git release tag.

See [THIRD_PARTY.md](THIRD_PARTY.md) and the retained font license under
`licenses/`. This snapshot does not add a new license grant; existing
third-party license terms continue to apply.

## Publication validation

Checks on 2026-11-03 use an independent macOS project directory with its own
downloaded SDK, compiler, Python environment and registry components.

- Bootstrap succeeds from the exported source and can be repeated without
  changing the pinned revisions or integration patches.
- All 749 retained third-party files match the original integrated source
  after download and patching.
- Fresh target configuration reproduces the original `dependencies.lock`.
  Generated SDK settings match, excluding App ID, App Certificate and Wi-Fi
  credentials replaced with placeholders.
- A full ESP32-S31 firmware build succeeds with the pinned environment.
- AEC/capture routing tests pass under ASan/UBSan in normal and bypass modes,
  including 160/256/512-sample chunks, continuity, resets and failure paths.
- Production UI and power-service tests pass with the real LVGL renderer and
  ASan/UBSan, including brightness, battery, Wi-Fi, physical-button fallback
  and repeated animated rendering.
- Firmware source and tests match the development commit. Source-only file
  checks exclude local credentials, SDKs, build output and diagnostic logs.

These checks use placeholder credentials and mocked hardware/services. They
do not pair a physical device, run a live Bot Station conversation or measure
acoustic echo reduction. See the README for board acceptance checks.
