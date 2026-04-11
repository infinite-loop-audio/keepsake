# G01.003 — Cross-Platform and Cross-Architecture

Date: 2026-04-10
Milestone: g01.003
Status: complete (macOS verified, Windows/Linux written but need CI)
Governing refs:
  - docs/contracts/004-ipc-bridge-protocol.md
  - docs/architecture/system-architecture.md

## What was done

### Platform abstraction layer (step 1)

Created `src/platform.h` — unified interface for all platform-specific code:

- **Process spawning:** `platform_spawn()` / `platform_kill()` — fork+exec on
  POSIX, CreateProcess on Windows
- **Pipe I/O:** `platform_write()` / `platform_read()` / `platform_read_ready()`
  — raw fd on POSIX, HANDLE+ReadFile/WriteFile on Windows
- **Shared memory:** `platform_shm_create()` / `platform_shm_open()` /
  `platform_shm_close()` — shm_open+mmap on POSIX,
  CreateFileMappingA+MapViewOfFile on Windows
- **Helpers:** `platform_shm_name()`, `platform_vst2_extension()`,
  `platform_is_vst2()`

All existing code (plugin.cpp, bridge_main.cpp, ipc.h) migrated to use the
platform abstraction.

### macOS cross-architecture bridge (step 4)

- Built `keepsake-bridge-x86_64` — x86_64 Mach-O executable that runs under
  Rosetta 2 on Apple Silicon
- CMake automatically builds it when `CMAKE_SYSTEM_PROCESSOR == arm64`
- Installed in `.clap` bundle at `Contents/Helpers/keepsake-bridge-x86_64`
- Factory scanner detects x86_64-only plugins (Mach-O header inspection),
  falls back to scanning via x86_64 bridge subprocess
- Bridge INIT protocol extended to return full metadata (uniqueID, name,
  vendor, version, category) so cross-architecture plugins have complete
  descriptors
- Factory selects the correct bridge binary at `create_plugin()` time

### Windows and Linux backends (steps 2–3)

Written in `platform.h` but not yet build-tested on those platforms:

- Windows: CreateProcess, CreatePipe, CreateFileMappingA, PeekNamedPipe
- Linux: identical POSIX path as macOS (minus CoreFoundation bundle resolution)
- CMakeLists.txt has platform branches for all three targets
- Linux may need `-lrt` for shm_open (added to CMake)

## Evidence

### Cross-architecture scan (macOS arm64 → x86_64 plugins)

```
Discovered 4 plugin(s):

  [0] STARK          id: keepsake.vst2.53646E39  vendor: Klevgrand             features: audio-effect
  [1] Massive        id: keepsake.vst2.4E694D61  vendor: Native Instruments    features: instrument synthesizer
  [2] Serum          id: keepsake.vst2.58667358  vendor: Xfer Records          features: instrument synthesizer
  [3] Vital          id: keepsake.vst2.56697461  vendor: Vital Audio           features: instrument synthesizer
```

Serum and Massive are x86_64-only — scanned via the Rosetta bridge with
correct uniqueIDs, vendor names, and feature classification.

### Bridge test (x86_64 plugin via Rosetta)

```
$ bridge-test keepsake-bridge-x86_64 "/Library/Audio/Plug-Ins/VST/Serum.vst"

[INIT] OK — in=0 out=2 params=315 flags=0x131
[ACTIVATE] OK
[PROCESS] DONE — Output peak: 0.000000 (silence, expected for synth without MIDI)
[SHUTDOWN] OK — Bridge exited with status 0
```

Full lifecycle completed through Rosetta 2.

### Bundle contents

```
keepsake.clap/Contents/
  Helpers/
    keepsake-bridge          — arm64 (native)
    keepsake-bridge-x86_64   — x86_64 (Rosetta)
  MacOS/
    keepsake                 — arm64 CLAP plugin
  Info.plist
```

## Validation

```
$ effigy qa          # all checks pass
```

## Known limits

- Windows and Linux backends written but not build-tested — need CI
- Version formatting heuristic misinterprets some vendor-specific encodings
  (e.g., NI Massive reports 24150 which doesn't map cleanly)
- 32-bit bridge binaries (Windows x86, Linux x86) not yet built — requires
  multilib toolchains

## Next Task

Author g01.004 — scan caching and config.toml, or GUI forwarding, depending
on priority. CI setup for Windows/Linux builds is a separate concern.
