# g02.003 — Windows ARM VM Exploratory Validation

Status: complete
Owner: Infinite Loop Audio
Date: 2026-04-12
Roadmap refs:
  - docs/roadmaps/g02/003-alpha-validation-matrix-and-evidence.md
Release refs:
  - docs/releases/v0.1-alpha-validation-matrix.md
  - docs/known-issues-v0.1-alpha.md

## Goal

Add first real Windows host evidence before `v0.1-alpha`:

- platform:
  - Windows 11 ARM64 VM under Parallels
- host:
  - REAPER x64
- target:
  - repo `test-plugin.dll` through Keepsake

This is exploratory evidence, not enough on its own to promote Windows to the
primary alpha support lane.

## Environment

- host machine:
  - Apple Silicon macOS
- VM:
  - Windows 11 ARM64
- REAPER:
  - `C:\Program Files\REAPER (x64)\reaper.exe`
- Keepsake install path:
  - `C:\Program Files\Common Files\CLAP\keepsake.clap`
  - `C:\Program Files\Common Files\CLAP\keepsake-bridge.exe`
- test plugin path:
  - `C:\Users\betterthanclay\VSTPlugins\test-plugin.dll`

## Setup

- installed Windows build tooling:
  - Git
  - CMake
  - Visual Studio 2022 Build Tools with C++ workload
- built Keepsake for `x64` on the ARM64 VM
- added Windows support for:
  - `clap-scan`
  - `test-plugin`
- configured Keepsake for targeted scan only:

```toml
[scan]
replace_default_vst2_paths = true
rescan = true
vst2_paths = ["C:~/VSTPlugins/test-plugin.dll"]

[expose]
mode = "all"
```

## Validation

### Targeted descriptor proof

Command shape:

```text
set "KEEPSAKE_VST2_PATH=C:\Users\betterthanclay\VSTPlugins\test-plugin.dll"
clap-scan.exe "C:\Program Files\Common Files\CLAP\keepsake.clap"
```

Result:

- Keepsake discovered exactly 1 bridged VST2 plugin
- descriptor:
  - `keepsake.vst2.4B505354`
  - `Keepsake Test Plugin`

### Real REAPER host proof

Used the repo ReaScript smoke flow against default REAPER config after clearing
stale Keepsake and REAPER CLAP caches.

Result:

- `PASS`
- timings:
  - scan found: `506 ms`
  - add FX finish: `2531 ms`
  - UI open finish: `3505 ms`
  - UI close: `5079 ms`

Observed REAPER FX name:

- `CLAP: Keepsake Test Plugin (Infinite Loop Audio)`

## Notes

- initial Windows REAPER scan showed stale APC metadata under `keepsake.clap`
  because `%APPDATA%\Keepsake\cache.dat` contained old descriptors
- clearing `cache.dat`, setting `rescan = true`, and deleting
  `reaper-clap-win64.ini` fixed the lane
- first Windows probe covered:
  - discovery
  - add FX
  - GUI open/close
- transport/audio was not exercised in this batch

## Release Read

Windows now has real host evidence, but only for:

- Windows 11 ARM64 VM
- x64 REAPER
- repo `test-plugin.dll`

That is enough to move Windows above CI-only status, but not enough to claim it
alongside the primary macOS lane for `v0.1-alpha`.

## Next Task

Extend the Windows exploratory lane with a transport/audio pass and, if
possible, one non-repo legacy plugin before widening any public Windows claim.
