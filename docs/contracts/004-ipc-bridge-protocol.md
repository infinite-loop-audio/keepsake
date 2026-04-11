# 004 - IPC Bridge Protocol

Status: active
Owner: Infinite Loop Audio
Updated: 2026-04-10
Depends on: docs/architecture/system-architecture.md, docs/contracts/002-clap-factory-interface.md
Authority owners: Infinite Loop Audio
Affects: g01.002 and all subsequent milestones that use the subprocess bridge

## Problem

Keepsake loads each plugin in an isolated subprocess for crash safety and
bitness bridging. The main process (the `.clap` binary hosted by the DAW) and
each bridge subprocess (loading the actual VST2/VST3/AU plugin) need a defined
protocol for control commands, audio data transfer, and failure handling.
Without a contract, the subprocess lifecycle, message format, and crash
semantics will be ad-hoc and fragile.

## Contract

### Architecture overview

```
keepsake.clap (main process, loaded by CLAP host)
    │
    ├── pipe: host → bridge (commands)
    ├── pipe: bridge → host (responses)
    ├── shared memory: audio pool (input + output buffers)
    │
    └── keepsake-bridge (subprocess, one per plugin instance)
            │
            └── VST2 plugin (loaded via VeSTige)
```

Each plugin instance gets its own subprocess. The main process spawns the
bridge, establishes two pipes and a shared memory segment, and communicates
via a binary message protocol.

### Subprocess lifecycle

```
IDLE  ──init──▶  LOADED  ──activate──▶  ACTIVE  ──start_processing──▶  PROCESSING
  │                │                      │                              │
  │                │                      │◀──stop_processing────────────┘
  │                │                      │
  │                │◀──deactivate─────────┘
  │                │
  │◀──shutdown─────┘
  │
  ▼
TERMINATED
```

States:
- **IDLE** — subprocess spawned, waiting for INIT
- **LOADED** — plugin loaded via VeSTige, metadata available
- **ACTIVE** — plugin activated with sample rate and buffer size, shared memory mapped
- **PROCESSING** — plugin is processing audio (process calls allowed)
- **TERMINATED** — subprocess exited (clean or crashed)

### Pipe protocol

Communication uses two unidirectional pipes: one host→bridge, one bridge→host.
Messages are binary with a fixed header:

```
[uint32_t opcode][uint32_t payload_size][payload bytes...]
```

All integers are native byte order (same architecture within a pipe pair).
Payload size may be zero. Maximum payload size: 65536 bytes.

#### Host → bridge opcodes

| Opcode | Name | Payload | Valid in state |
|---|---|---|---|
| `0x01` | INIT | `uint32_t path_len` + path bytes (no null terminator) | IDLE |
| `0x02` | SET_SHM | `uint32_t name_len` + shm name bytes + `uint32_t shm_size` | LOADED |
| `0x03` | ACTIVATE | `double sample_rate` + `uint32_t max_frames` | LOADED (after SET_SHM) |
| `0x04` | PROCESS | `uint32_t num_frames` | PROCESSING |
| `0x05` | SET_PARAM | `uint32_t param_index` + `float value` | ACTIVE or PROCESSING |
| `0x06` | STOP_PROC | (none) | PROCESSING |
| `0x07` | START_PROC | (none) | ACTIVE |
| `0x08` | DEACTIVATE | (none) | ACTIVE |
| `0x09` | SHUTDOWN | (none) | any except TERMINATED |
| `0x0A` | MIDI_EVENT | `int32_t delta_frames` + `uint8_t data[4]` | PROCESSING |

#### Bridge → host opcodes

| Opcode | Name | Payload | Sent in response to |
|---|---|---|---|
| `0x81` | OK | (none) | INIT, SET_SHM, ACTIVATE, DEACTIVATE, START_PROC, STOP_PROC |
| `0x82` | ERROR | `uint32_t msg_len` + message bytes | any command that fails |
| `0x84` | PROCESS_DONE | (none) | PROCESS |

#### Protocol rules

- Every host→bridge command (except PROCESS) expects exactly one response (OK
  or ERROR) before the next command may be sent.
- PROCESS expects exactly one PROCESS_DONE response. The host must not send
  another PROCESS until PROCESS_DONE is received.
- SET_PARAM and MIDI_EVENT may be sent between PROCESS/PROCESS_DONE pairs while
  in PROCESSING state (queued by bridge, applied at next PROCESS).
- SHUTDOWN may be sent in any state. The bridge should respond OK and exit.
- If the bridge receives a command invalid for its current state, it responds
  ERROR and remains in its current state.

### Shared memory layout

The host creates a named shared memory segment and sends its name to the bridge
via SET_SHM. The layout is fixed at ACTIVATE time:

```
Offset 0:
  float input_buffers[num_inputs][max_frames]

Offset (num_inputs * max_frames * sizeof(float)):
  float output_buffers[num_outputs][max_frames]
```

- `num_inputs` and `num_outputs` are read from the plugin's `AEffect` during INIT.
- `max_frames` is set during ACTIVATE.
- The total size is `(num_inputs + num_outputs) * max_frames * sizeof(float)`.
- Input buffers are written by the host before sending PROCESS.
- Output buffers are written by the bridge during processing and read by the
  host after PROCESS_DONE.

Channel ordering: channels are contiguous (non-interleaved), matching the
VST2 `float**` convention.

### Shared memory naming

The shared memory segment name follows the pattern:

```
/keepsake-<pid>-<instance_id>
```

Where `<pid>` is the main process PID and `<instance_id>` is a unique
per-instance counter. This ensures no collisions between multiple Keepsake
instances or multiple plugin instances.

Platform implementation:
- macOS / Linux: `shm_open()` + `mmap()` + `shm_unlink()`
- Windows: `CreateFileMappingA()` + `MapViewOfFile()` + `CloseHandle()`

### Process cycle

The real-time audio processing cycle for a single block:

1. Host writes input audio to `input_buffers` in shared memory
2. Host sends `PROCESS { num_frames }` on the command pipe
3. Bridge reads input from shared memory
4. Bridge calls `processReplacing()` on the VST2 plugin
5. Bridge writes output to `output_buffers` in shared memory
6. Bridge sends `PROCESS_DONE` on the response pipe
7. Host reads output audio from shared memory

The pipe read/write for PROCESS/PROCESS_DONE is the synchronization mechanism.
No separate semaphores are needed — the pipe itself blocks until data is
available.

### Crash handling

If the bridge subprocess crashes or becomes unresponsive:

- **Pipe EOF:** If `read()` on the response pipe returns 0 (EOF), the bridge
  has crashed. The host must:
  1. Output silence for the current and all subsequent process calls
  2. Report an error state to the CLAP host (via `clap_host_t->request_restart`
     or by returning `CLAP_PROCESS_ERROR` from process)
  3. Clean up shared memory and pipe file descriptors
  4. `waitpid()` the subprocess to avoid zombies

- **Timeout:** If PROCESS_DONE is not received within a reasonable timeout
  (suggested: 5 seconds for non-RT commands, 2x the buffer duration for
  PROCESS), the host should:
  1. Kill the subprocess (SIGKILL / TerminateProcess)
  2. Follow the same cleanup as pipe EOF

- **Recovery:** The host may attempt to restart the bridge by spawning a new
  subprocess and re-initializing. This is optional for g01.002.

### Bridge helper binary

The bridge helper is a separate executable:

- `keepsake-bridge` (64-bit, same architecture as the .clap)
- Future: `keepsake-bridge-32` (32-bit, for bitness bridging)

The binary is located adjacent to the `.clap` bundle or in a known path.
On macOS, it can be placed inside the `.clap` bundle at
`Contents/Helpers/keepsake-bridge`.

The bridge reads commands from stdin (fd 0) and writes responses to stdout
(fd 1). The host spawns it with pipes connected to stdin/stdout.
Stderr (fd 2) is inherited from the host for diagnostic logging.

### Threading rules

| Operation | Thread |
|---|---|
| Subprocess spawn/kill | Main thread only |
| INIT, ACTIVATE, DEACTIVATE, SHUTDOWN | Main thread only |
| SET_SHM | Main thread only |
| PROCESS, SET_PARAM, MIDI_EVENT | Audio thread |
| START_PROC, STOP_PROC | Audio thread |
| Shared memory read/write | Audio thread (during process cycle) |

The main process must not send non-RT commands while a PROCESS is in flight.
The CLAP lifecycle guarantees this: activate/deactivate happen on the main
thread while processing is stopped.

## Validation

- Verify that a subprocess can be spawned, receives INIT, loads a VST2 plugin,
  and responds OK.
- Verify that shared memory is created, mapped by both processes, and audio
  data round-trips correctly.
- Verify that the process cycle produces correct audio output from a real
  VST2 plugin.
- Verify that killing the subprocess causes the host to detect the crash
  (pipe EOF) and output silence.
- Verify that the protocol rejects commands invalid for the current state.

## Roadmap Impact

- g01.002: First implementation of this protocol
- All subsequent milestones that add MIDI, state save/restore, GUI forwarding,
  or cross-architecture bridging

## Planning Notes

This protocol is intentionally simple for the initial implementation. Carla
uses a more sophisticated approach (ring buffers in shared memory for RT
control, separate shared memory regions for different concerns). Keepsake can
adopt those optimizations in later milestones if pipe latency proves
insufficient.

The pipe-as-synchronization approach avoids platform-specific semaphore issues
(macOS's incomplete POSIX semaphore support, Windows's different API).
A single-byte pipe read/write has negligible overhead compared to audio
processing time.

## Next Task

Land g01.002 — implement the bridge binary, pipe protocol, shared memory
audio transfer, and CLAP plugin lifecycle.
