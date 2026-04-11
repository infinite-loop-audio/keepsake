# G01.001 — CLAP Factory and VeSTige Loader PoC

Date: 2026-04-10
Milestone: g01.001
Status: complete
Governing refs:
  - docs/contracts/001-working-rules.md
  - docs/contracts/002-clap-factory-interface.md
  - docs/architecture/system-architecture.md

## What was done

Built the Keepsake CLAP plugin from scratch: CMake build system, CLAP SDK
integration, VeSTige header vendoring, VeSTige-based VST2 metadata loader,
CLAP plugin factory, and platform-aware scanning.

### Build system (step 1)

- CMake project with FetchContent for CLAP SDK 1.2.2
- VeSTige header vendored from Carla lineage (GPL v2+)
- Produces `keepsake.clap` bundle on macOS (arm64)
- Supports macOS, Windows, and Linux target configurations
- `clap_entry` symbol exported, all other symbols hidden

### CLAP entry and factory (step 2)

- `clap_entry` with init/deinit/get_factory
- `clap_plugin_factory_t` with get_plugin_count, get_plugin_descriptor,
  create_plugin (returns NULL — audio bridge deferred to g01.002)
- Descriptors follow contract 002 shape: `keepsake.vst2.<hex>` IDs, name,
  vendor, version, features

### VeSTige loader (step 3)

- Loads real VST2 `.vst` bundles on macOS via CFBundle resolution
- Extracts metadata via VeSTige dispatcher: effGetEffectName, effGetVendorString,
  effGetProductString, effGetVendorVersion, effGetPlugCategory
- Validates `kEffectMagic` (0x56737450)
- Fallback chain per contract 002: name → product → filename stem; empty vendor → "Unknown"
- Architecture detection: reads Mach-O headers (both thin and fat binaries) to
  identify x86_64 vs arm64 mismatches and reports them clearly instead of
  cryptic dlopen errors

### Factory integration (step 4)

- Scans platform-default VST2 paths or `KEEPSAKE_VST2_PATH` environment variable
- Builds descriptors per contract 002 with uniqueID collision detection
- Feature mapping: category + flags → CLAP feature strings (instrument,
  synthesizer, audio-effect, etc.)
- Version formatting: hex-nibble heuristic handles both 0xMMmmpp and decimal
  MMMmmpp conventions

## Evidence

### Build

```
$ cmake --preset default && cmake --build --preset default
-- CLAP version: 1.2.2
-- Configuring done
-- Generating done
[100%] Built target keepsake
```

Output: `build/keepsake.clap/Contents/MacOS/keepsake` — Mach-O 64-bit bundle arm64

### clap-scan test (restricted set)

```
$ KEEPSAKE_VST2_PATH=/tmp/keepsake-test-vst ./build/clap-scan ./build/keepsake.clap

keepsake: scanning 1 path(s) for VST2 plugins
keepsake: loaded 'STARK.vst' — id=0x53646E39 name='STARK' vendor='Klevgrand' category=1
keepsake: loaded 'Endless Smile.vst' — id=0x454E4453 name='Endless Smile' vendor='Dada Life' category=1
keepsake: loaded 'Vital.vst' — id=0x56697461 name='Vital' vendor='Vital Audio' category=2
keepsake: found 3 VST2 plugin(s)

Discovered 3 plugin(s):

  [0] STARK         id: keepsake.vst2.53646E39  vendor: Klevgrand   version: 1.2.3   features: audio-effect
  [1] Endless Smile  id: keepsake.vst2.454E4453  vendor: Dada Life   version: 1.3.10  features: audio-effect
  [2] Vital          id: keepsake.vst2.56697461  vendor: Vital Audio version: 1.5.5   features: instrument synthesizer
```

### Architecture detection (mixed set)

```
keepsake: loaded 'STARK.vst' — id=0x53646E39 name='STARK' vendor='Klevgrand' category=1
keepsake: skipping 'APC.vst' — x86_64 binary (needs subprocess bridge for cross-architecture loading)
keepsake: skipping 'AGML2.vst' — x86_64 binary (needs subprocess bridge for cross-architecture loading)
keepsake: skipping 'Serum.vst' — x86_64 binary (needs subprocess bridge for cross-architecture loading)
keepsake: loaded 'Vital.vst' — id=0x56697461 name='Vital' vendor='Vital Audio' category=2
```

Fat binaries (x86_64+i386 without arm64) and thin x86_64 binaries are both
correctly identified and reported with clear messages.

### Full system scan

Scanned `/Library/Audio/Plug-Ins/VST/` with ~50 plugins installed:
- ~20 arm64-compatible plugins loaded successfully with correct metadata
- ~20 x86_64-only plugins correctly identified and skipped
- 3 plugins returned null from entry point (vendor-specific initialization issues)
- 3 fat binaries (x86_64+i386 only) correctly detected as architecture mismatch

### Installed

`keepsake.clap` installed to `~/Library/Audio/Plug-Ins/CLAP/` for CLAP host
testing. REAPER and Studio One 7 are available on this system.

## Validation

```
$ effigy qa        # all checks pass
$ effigy qa:northstar  # all checks pass
```

## Files created

- `CMakeLists.txt` — build system
- `CMakePresets.json` — configure/build presets
- `cmake/Info.plist.in` — macOS bundle plist template
- `vendor/vestige/vestige.h` — VeSTige header (Carla lineage, GPL v2+)
- `vendor/vestige/NOTICE` — VeSTige provenance and license notice
- `src/main.cpp` — CLAP entry point
- `src/factory.h` / `src/factory.cpp` — CLAP plugin factory
- `src/vst2_loader.h` / `src/vst2_loader.cpp` — VeSTige-based VST2 loader
- `tools/clap-scan.cpp` — development scanner tool

## Known limits

- **No audio processing** — `create_plugin()` returns NULL. Audio bridge and
  subprocess isolation are g01.002 scope.
- **No cross-architecture loading** — x86_64 plugins on ARM (and vice versa)
  are detected and skipped. The subprocess bridge will solve this.
- **In-process loading** — some plugins crash or hang during in-process
  initialization. The subprocess model eliminates this risk.
- **No scan caching** — every init rescans. Caching is future work.
- **Version format heuristic** — works for common conventions but may
  misinterpret rare encodings.
- **VeSTige header is GPL v2+** — the project LICENSE says LGPL v2.1 but the
  combined binary is GPL v2+ due to the VeSTige header inclusion.

## Next Task

Author g01.002 (audio processing bridge and subprocess isolation) — this is
the natural next milestone now that the factory and loader are proven. The
subprocess bridge also resolves the cross-architecture and crash isolation
requirements.
