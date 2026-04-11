# G01.002 — Audio Bridge and Subprocess Isolation

Status: complete
Owner: Infinite Loop Audio
Updated: 2026-04-10
Governing refs:
  - docs/contracts/001-working-rules.md
  - docs/contracts/002-clap-factory-interface.md
  - docs/contracts/004-ipc-bridge-protocol.md
  - docs/architecture/system-architecture.md
Auto-continuation: allowed within g01

## Scope

Implement the subprocess bridge so that Keepsake can actually instantiate and
process audio through VST2 plugins. After this milestone, a real VST2 plugin
produces real audio output in a real CLAP host, running in an isolated
subprocess.

This milestone builds on g01.001 (working factory and VeSTige loader) and
implements contract 004 (IPC bridge protocol).

This milestone does **not** include:
- Cross-architecture bridging (32-bit helper, x86_64↔arm64) — deferred to a
  later milestone
- GUI / editor forwarding — deferred
- Plugin state save/restore (get/set chunk via CLAP state extension) — deferred
- Full MIDI support — basic note-on/note-off is sufficient
- Scan caching — scanning stays in-process
- Crash recovery (auto-restart of dead subprocesses) — deferred; detect and
  silence is sufficient

## Steps

### 1. Bridge helper binary

Create `keepsake-bridge`, a standalone executable that:

- Reads the IPC pipe protocol from stdin, writes responses to stdout
- Receives INIT with a VST2 plugin path, loads it via VeSTige (reusing
  loader code from g01.001)
- Calls `effOpen` on the loaded plugin
- Receives SET_SHM, opens the named shared memory segment
- Receives ACTIVATE, calls `effSetSampleRate`, `effSetBlockSize`,
  `effMainsChanged(1)` on the VST2 plugin
- Receives PROCESS, reads input audio from shared memory, calls
  `processReplacing()`, writes output to shared memory, responds PROCESS_DONE
- Receives SET_PARAM, calls `setParameter()` on the VST2 plugin
- Receives MIDI_EVENT, queues MIDI events for the next process call
  (via `effProcessEvents`)
- Receives DEACTIVATE, calls `effMainsChanged(0)`
- Receives SHUTDOWN, calls `effClose`, exits cleanly

CMake target: `keepsake-bridge` executable, placed inside the `.clap` bundle
on macOS at `Contents/Helpers/keepsake-bridge`.

Acceptance:
- `keepsake-bridge` compiles as a standalone executable
- Given a pipe-connected parent process, it can load a real VST2 plugin
  and respond to the protocol correctly

### 2. Shared memory infrastructure

Implement cross-platform shared memory creation and mapping:

- `shm_create(name, size)` — creates a named shared memory segment
- `shm_open_existing(name, size)` — opens an existing segment by name
- `shm_close(handle)` — unmaps and unlinks

The host creates the segment (naming per contract 004:
`/keepsake-<pid>-<instance>`), sends the name to the bridge via SET_SHM,
and the bridge maps it.

Audio buffer layout per contract 004: non-interleaved float channels,
inputs followed by outputs.

Acceptance:
- Shared memory round-trips audio data correctly between two processes
- Segment is cleaned up on both normal shutdown and crash

### 3. IPC pipe protocol implementation

Implement the binary pipe protocol from contract 004:

- Message framing: `[uint32_t opcode][uint32_t size][payload]`
- Host-side: `BridgeClient` class that manages subprocess lifecycle, pipes,
  and shared memory. Provides methods matching the protocol opcodes.
- Bridge-side: message loop reading from stdin, dispatching to handler
  functions, writing responses to stdout.
- Timeout handling: use `poll()` / `select()` with timeout on pipe reads
  for crash detection.

Acceptance:
- Host sends INIT → bridge loads plugin → bridge responds OK
- Host sends ACTIVATE → bridge activates plugin → bridge responds OK
- Host sends PROCESS → bridge processes → bridge responds PROCESS_DONE
- Host sends SHUTDOWN → bridge exits cleanly

### 4. CLAP plugin implementation

Implement `clap_plugin_t` so that `factory->create_plugin()` returns a working
plugin instance:

- `create_plugin(plugin_id)` — look up the plugin path from the factory's
  scan results, spawn `keepsake-bridge`, send INIT
- `plugin->init()` — verify bridge responded OK to INIT
- `plugin->activate(sample_rate, min_frames, max_frames)` — create shared
  memory, send SET_SHM + ACTIVATE
- `plugin->start_processing()` — send START_PROC
- `plugin->process(clap_process_t)` — copy input audio to shared memory,
  send PROCESS, wait for PROCESS_DONE, copy output audio from shared memory
  to the CLAP output buffers. Handle basic MIDI note events from the CLAP
  event list.
- `plugin->stop_processing()` — send STOP_PROC
- `plugin->deactivate()` — send DEACTIVATE, free shared memory
- `plugin->destroy()` — send SHUTDOWN, wait for subprocess exit, cleanup
- `plugin->get_extension()` — return audio-ports extension (declare correct
  channel counts from the VST2 plugin's numInputs/numOutputs)

The audio-ports CLAP extension must report the correct channel configuration
so the host routes audio correctly.

Acceptance:
- A CLAP host can instantiate a Keepsake-bridged plugin
- The plugin activates, processes audio, and deactivates without errors
- Audio from the VST2 plugin is audible in the host

### 5. Crash isolation verification

Verify that a crashed subprocess produces silence rather than crashing the
host:

- Force-kill the bridge subprocess while processing
- Verify the host detects the crash (pipe EOF)
- Verify the host outputs silence for subsequent process calls
- Verify the host does not crash or hang

Acceptance:
- Killing the bridge subprocess during audio processing does not crash the
  CLAP host
- The host outputs silence after detecting the crash

### 6. Evidence collection

- Record audio output from a real VST2 plugin playing through the bridge
  in a real CLAP host
- Log the subprocess spawn, IPC round-trip, and audio processing evidence
- Record crash isolation test results

Acceptance:
- Evidence exists in `docs/logs/` demonstrating real audio output from a
  bridged VST2 plugin
- Evidence includes crash isolation verification

## Stop Conditions

- Stop if the pipe protocol cannot achieve acceptable latency for real-time
  audio (measure and report; consider shared-memory semaphores as fallback)
- Stop if subprocess spawning is too slow for host scan/instantiation
  expectations
- Stop if VeSTige loading in the subprocess requires different initialization
  than the in-process loader (investigate before redesigning)
- Stop on any legal hard stop (Steinberg SDK usage)

## Evidence Requirements

- Real VST2 plugin producing audible audio in a CLAP host via the subprocess
  bridge
- Crash isolation: subprocess death → silence, host stays up
- Build succeeds from clean checkout
- IPC round-trip timing measurement (for future optimization baseline)

## Next Task

After this milestone closes: author g01.003 for cross-architecture bridging
(32-bit helper binary, x86_64↔arm64 on macOS), or author g01.003 for scan
caching and configuration, depending on priority.
