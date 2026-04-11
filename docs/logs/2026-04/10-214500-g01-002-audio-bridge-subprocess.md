# G01.002 — Audio Bridge and Subprocess Isolation

Date: 2026-04-10
Milestone: g01.002
Status: complete
Governing refs:
  - docs/contracts/004-ipc-bridge-protocol.md
  - docs/contracts/002-clap-factory-interface.md

## What was done

Implemented the subprocess bridge: keepsake-bridge helper binary, IPC pipe
protocol, shared memory audio transfer, and full CLAP plugin lifecycle
(init → activate → process → deactivate → destroy). A real VST2 effect plugin
(STARK by Klevgrand) processes real audio through the subprocess bridge.

### IPC protocol (step 1–3)

- `src/ipc.h` — protocol constants, pipe I/O helpers, shared memory helpers
  (single header shared between plugin and bridge)
- Binary message protocol: `[opcode][size][payload]` over pipes
- POSIX shared memory (`shm_open` + `mmap`) for audio buffers
- Named segments: `/keepsake-<pid>-<instance>`

### Bridge helper binary (step 1)

- `src/bridge_main.cpp` — standalone executable reading commands from stdin
- Loads VST2 plugins via VeSTige, calls effOpen/effSetSampleRate/effSetBlockSize/
  effMainsChanged/processReplacing/effClose
- MIDI event queuing via effProcessEvents
- Full lifecycle: INIT → SET_SHM → ACTIVATE → START_PROC → PROCESS → STOP_PROC
  → DEACTIVATE → SHUTDOWN
- Installed at `keepsake.clap/Contents/Helpers/keepsake-bridge`

### CLAP plugin implementation (step 4)

- `src/plugin.h` + `src/plugin.cpp` — full `clap_plugin_t` implementation
- Spawns bridge subprocess via fork/exec with pipe redirection
- `create_plugin()` in factory now returns working plugin instances
- Audio-ports CLAP extension reports correct channel configuration
- MIDI forwarding from CLAP events to bridge via IPC_OP_MIDI_EVENT
- Crash detection: pipe EOF or timeout → silence + CLAP_PROCESS_ERROR

## Evidence

### Bridge integration test (STARK — stereo effect)

```
=== Bridge test (pid=77355) ===

[INIT] OK — in=2 out=2 params=40 flags=0x231
[SET_SHM] OK
[ACTIVATE] OK
[START_PROC] OK
[PROCESS] Sending 256 frames...
[PROCESS] DONE
[PROCESS] Output peak: 0.669037 (AUDIO PRESENT)
[STOP_PROC] OK
[DEACTIVATE] OK
[SHUTDOWN] OK

Bridge exited with status 0
```

A 440 Hz sine wave was written to shared memory input buffers, PROCESS was
sent, and the output contained real processed audio (peak 0.669). The VST2
plugin ran in an isolated subprocess.

### Bridge integration test (Vital — synth)

```
[INIT] OK — in=0 out=2 params=775 flags=0x131
[PROCESS] Output peak: 0.000000 (silence)
[SHUTDOWN] OK — Bridge exited with status 0
```

Vital loaded correctly as a zero-input synth. Silence is expected without MIDI
input. Full lifecycle completed cleanly.

### Build

```
$ cmake --build build
[100%] Built target keepsake
[100%] Built target keepsake-bridge
[100%] Built target clap-scan
[100%] Built target bridge-test
```

### Installation

```
keepsake.clap/Contents/
  Helpers/keepsake-bridge    — bridge subprocess binary
  MacOS/keepsake             — CLAP plugin binary
  Info.plist
```

Installed to `~/Library/Audio/Plug-Ins/CLAP/`.

## Validation

```
$ effigy qa          # all checks pass
$ effigy qa:northstar  # all checks pass
```

## Files created

- `src/ipc.h` — IPC protocol, pipe I/O, shared memory
- `src/bridge_main.cpp` — bridge subprocess executable
- `src/plugin.h` / `src/plugin.cpp` — CLAP plugin instance
- `tools/bridge-test.cpp` — bridge integration test tool

## Files modified

- `src/factory.h` / `src/factory.cpp` — create_plugin now returns working
  instances, bridge path resolution, plugin info lookup
- `CMakeLists.txt` — bridge target, bridge-test target, bundle installation

## Known limits

- **No GUI forwarding** — plugin editors not supported yet
- **Basic MIDI only** — note on/off via CLAP_EVENT_MIDI; no note expressions,
  parameter automation via CLAP events, or sysex
- **No state save/restore** — CLAP state extension not implemented
- **No scan caching** — every init rescans
- **No cross-architecture** — x86_64 plugins on ARM are detected but not
  bridged (needs x86_64 bridge helper binary)
- **No crash recovery** — crashed bridge stays dead (no auto-restart)
- **Blocking audio thread** — pipe read blocks during process; acceptable for
  POC but could be optimized with ring buffers

## Next Task

Author g01.003 for one of: cross-architecture bridging (x86_64 bridge helper
under Rosetta), scan caching and config.toml support, or GUI forwarding. The
subprocess bridge is now working end-to-end.
