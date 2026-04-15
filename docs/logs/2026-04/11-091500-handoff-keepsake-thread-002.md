---
title: Keepsake thread-002 handoff
status: active
owner: Infinite Loop Audio
updated: 2026-04-11
tags: [coordination, handoff]
---

## What This Thread Was Doing

This thread picked up from thread-001's planning-only repo and executed the full initial build of Keepsake — from zero code to a working multi-format CLAP plugin bridge. The arc was:

1. Authored the CLAP factory interface contract (002) and the first milestone card (g01.001), then immediately executed: CMake build system, VeSTige header vendoring, CLAP entry point, factory, and metadata loader.
2. The user broadened the scope from VST2-only to multi-format (VST2 + VST3 + AU v2) with 32-bit bridging as a first-class concern. Updated all authority surfaces.
3. Authored the IPC bridge protocol contract (004) and g01.002, then executed: bridge subprocess binary, pipe protocol, shared memory audio, full CLAP plugin lifecycle. STARK processed real audio through the bridge.
4. Built the platform abstraction layer and macOS cross-architecture bridge (x86_64 under Rosetta). Serum and Massive loaded through the Rosetta bridge with full metadata.
5. Added scan caching, config.toml, and rescan sentinel support.
6. Implemented MIDI (CLAP note events → VST2 MIDI), parameter automation (CLAP params extension), and state save/restore (CLAP state extension via effGetChunk/effSetChunk).
7. Implemented floating window GUI forwarding (macOS Cocoa, CLAP GUI extension).
8. Refactored the bridge into a format-agnostic loader abstraction, added AU v2 enumeration and loading. 462 AU plugins discovered alongside VST2.

Seven of nine g01 milestones completed in a single session.

## Why It Matters

Keepsake is the clean path for Signal users (and any CLAP host users) to keep using legacy plugins. Without it, valuable VST2 instruments and effects are stranded as hosts and operating systems move forward. The broader multi-format scope (VST3, AU, 32-bit) makes Keepsake a universal plugin bridge, not just a VST2 lifeline.

The work delivered in this thread takes Keepsake from "planning documents" to "working plugin bridge with real audio output, cross-architecture support, and multi-format enumeration." The next thread inherits a functional codebase, not just specs.

## Current State

- Done so far:
  - g01.001: CMake build system, CLAP factory, VeSTige loader, Mach-O architecture detection
  - g01.002: Bridge subprocess, IPC pipe protocol, shared memory audio, CLAP plugin lifecycle
  - g01.003: Platform abstraction (macOS/Windows/Linux), x86_64 bridge under Rosetta
  - g01.004: Scan caching (mtime-based), config.toml, rescan sentinel
  - g01.005: CLAP note events → MIDI, parameter automation, state save/restore
  - g01.006: Floating window GUI forwarding (macOS Cocoa, CLAP GUI extension)
  - g01.008: Bridge loader abstraction, AU v2 enumeration + loading, multi-format factory
  - Contracts 002 (CLAP factory interface) and 004 (IPC bridge protocol) authored and governing
  - All milestones logged in `docs/logs/2026-04/`
  - `effigy qa` passing, `effigy doctor` has expected warnings (file sizes, TODO markers)

- Still open:
  - g01.007: CI and cross-platform testing — deferred by user until functionality can be tested directly
  - g01.009: 32-bit bridge binaries — planned, not yet started
  - VST3 SDK integration — architecture ready (loader interface, scan paths, bridge dispatch), SDK not fetched
  - Windows/Linux GUI backends — stubs exist, need Win32/X11 implementations
  - Windows/Linux build testing — platform abstraction written, not verified on those platforms
  - VeSTige header is GPL v2+ but project LICENSE says LGPL v2.1 — needs resolution
  - God-file warnings: plugin.cpp (579 lines) and factory.cpp (472 lines) should be split

- Active spec lane: none — all planning material has been promoted into architecture and contracts

- Canonical refs for execution:
  - `~/Dev/projects/keepsake/docs/contracts/001-working-rules.md`
  - `~/Dev/projects/keepsake/docs/contracts/002-clap-factory-interface.md`
  - `~/Dev/projects/keepsake/docs/contracts/004-ipc-bridge-protocol.md`
  - `~/Dev/projects/keepsake/docs/architecture/system-architecture.md`
  - `~/Dev/projects/keepsake/docs/architecture/product-guardrails.md`
  - `~/Dev/projects/keepsake/docs/vision/001-keepsake-vision.md`

- Remaining continuation envelope: g01.009 (32-bit bridge) is planned but not marked ready. g01.007 (CI) is deferred by user preference. Both can be picked up without new planning.

- Lane budget / pause signal: this thread paused because context depth warranted a fresh start, not because work was blocked. The next thread can continue executing immediately.

- Key files:
  - `~/Dev/projects/keepsake/AGENTS.md`
  - `~/Dev/projects/keepsake/CMakeLists.txt`
  - `~/Dev/projects/keepsake/src/main.cpp` — CLAP entry point
  - `~/Dev/projects/keepsake/src/factory.h` / `factory.cpp` — CLAP factory + multi-format scanning
  - `~/Dev/projects/keepsake/src/plugin.h` / `plugin.cpp` — CLAP plugin instance
  - `~/Dev/projects/keepsake/src/bridge_main.cpp` — bridge subprocess
  - `~/Dev/projects/keepsake/src/bridge_loader.h` — format-agnostic loader interface
  - `~/Dev/projects/keepsake/src/bridge_loader_vst2.cpp` — VST2 loader
  - `~/Dev/projects/keepsake/src/bridge_loader_au.mm` — AU v2 loader
  - `~/Dev/projects/keepsake/src/ipc.h` — IPC protocol + pipe I/O
  - `~/Dev/projects/keepsake/src/platform.h` — cross-platform abstraction
  - `~/Dev/projects/keepsake/src/bridge_gui.h` / `bridge_gui_mac.mm` — GUI forwarding
  - `~/Dev/projects/keepsake/src/config.h` / `config.cpp` — config + cache
  - `~/Dev/projects/keepsake/src/vst2_loader.h` / `vst2_loader.cpp` — metadata extraction
  - `~/Dev/projects/keepsake/vendor/vestige/vestige.h` — VeSTige ABI header
  - `~/Dev/projects/keepsake/docs/roadmaps/g01/README.md`

## Boundaries

- Stay within g01 — the generation covers all remaining work (007, 009, plus any new milestones)
- Do not use or reference the Steinberg VST2 SDK — VeSTige only (legal hard stop)
- CLAP remains the outer plugin format — no VST3 outer format
- VST3 SDK (GPLv3) must run in the bridge subprocess only (license boundary at process edge)
- Signal integration work belongs in the Signal repo, not here
- Follow repo constraints from [AGENTS.md](~/Dev/projects/keepsake/AGENTS.md)

## Important Context

**Planning lineage:** Thread-001 did all foundation planning (vision, architecture, contracts, Northstar spine). Thread-002 authored the remaining contracts (002, 004) and executed milestones 001–006 + 008. The planning surfaces are current and governing.

**Key decisions made in this thread:**

| Decision | Choice | Why |
|---|---|---|
| Scope expansion | VST2 + VST3 + AU v2 + 32-bit | User directed; Keepsake is a universal bridge, not VST2-only |
| AU scope | AU v2 first, AUv3 later if needed | User directed |
| 32-bit priority | First-class from the start | User directed; shapes subprocess architecture |
| IPC mechanism | Pipes for control + shared memory for audio | Matches Carla pattern; pipes as synchronization avoids platform-specific semaphore issues |
| GUI approach | Floating window (not embedded) | Cross-process window embedding is too complex for POC; floating works on all platforms |
| Bridge architecture | Single binary, format-aware via INIT payload | Cleaner than separate binaries per format |
| VST3 scan | Disabled until SDK integrated | Bridge can't load .vst3 without VST3 SDK; scan attempts block on subprocess timeout |
| Cross-arch scanning | Via bridge subprocess | x86_64 bridge under Rosetta extracts metadata from x86_64-only plugins |
| Generation scope | g01 is long-lived (9+ milestones) | User directed; "generations should be much larger than 4 roadmaps" |

**User preferences:**
- Group work into meaningful batches, not tiny changes
- Don't run large test suites after every update — complete solid chunks then test
- Assess for churn every few messages; refocus on broad goals when work gets too atomic
- Provide "next" suggestions at the end of each task
- The user wants to test functionality directly before setting up CI (hence g01.007 deferred)

**Open tensions:**
- VeSTige header license (GPL v2+) vs project license (LGPL v2.1) — not yet resolved
- VST3 SDK integration complexity — the SDK has multiple sub-repos and its own build system; FetchContent may be complex
- God-file warnings — plugin.cpp and factory.cpp should eventually be split but aren't blocking
- Some VST2 plugins crash or hang during in-process scanning (Loopcloud, BFDPlayer) — subprocess-based scanning would fix this but isn't implemented for VST2 yet (only cross-arch uses bridge scanning)
- Version number heuristic misinterprets some vendor-specific encodings (NI Massive)

## Suggested Next Move

Start by running the orientation sequence:

```sh
effigy tasks
effigy doctor
```

Then read the g01 roadmap:
- `docs/roadmaps/g01/README.md` — milestone table and sequencing intent

The immediate options for the next thread, in suggested priority:

1. **Mark g01.009 ready and execute** — 32-bit bridge binaries for Windows/Linux. The architecture is in place (platform abstraction, bridge loader interface); this is primarily a cross-compilation and IPC struct alignment exercise.

2. **Integrate the VST3 SDK** — FetchContent the VST3 SDK into the bridge, implement `bridge_loader_vst3.cpp`, enable the VST3 scan paths in the factory. This completes the multi-format vision.

3. **Refactor god-files** — Split plugin.cpp and factory.cpp into smaller, focused modules. Not urgent but improves maintainability.

4. **Mark g01.007 ready and execute** — CI setup with GitHub Actions. The user deferred this but may want it now that core functionality is in place.

The user should be asked which of these to prioritize, or whether to test the existing build in a real CLAP host first (REAPER and Studio One 7 are installed on this system).

## Completion Protocol

1. All completed milestone cards (001–006, 008) are marked `complete` in the roadmap.
2. Roadmap and log surfaces reflect the current state — `docs/roadmaps/g01/README.md` shows 7 complete, 2 remaining.
3. g01.009 continuation envelope is in-bounds. g01.007 is deferred by user preference but in-bounds.
4. This thread paused due to context depth, not a stop signal or budget exhaustion. The next thread starts fresh.
5. Unresolved risks: VeSTige license (GPL v2+ vs LGPL v2.1), VST3 SDK integration complexity, god-file warnings, in-process scan crashes for some plugins.
6. Next task: execute g01.009 (32-bit bridge) or integrate the VST3 SDK, depending on user priority.
