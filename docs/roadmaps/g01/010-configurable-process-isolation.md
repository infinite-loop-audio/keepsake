# G01.010 — Configurable Process Isolation

Status: complete
Owner: Infinite Loop Audio
Updated: 2026-04-11
Governing refs:
  - docs/contracts/006-process-isolation-policy.md
  - docs/contracts/004-ipc-bridge-protocol.md
Auto-continuation: allowed within g01

## Scope

Refactor the bridge from one-process-per-instance to a multi-instance plugin
server with configurable isolation. After this milestone, the default behaviour
groups plugins into a shared process for efficiency, with per-plugin overrides
for isolation.

## Steps

### 1. Multi-instance bridge

Refactor `bridge_main.cpp` to manage multiple plugin instances:

- Instance table: `std::unordered_map<uint32_t, BridgeLoader*>`
- INIT creates a new instance, assigns an ID, returns it in the OK response
- All other opcodes dispatch to the correct instance by ID
- SHUTDOWN with ID=0 exits the process; with a specific ID, destroys that
  instance only
- Each instance has its own shared memory segment

Acceptance:
- Bridge can host two plugin instances simultaneously
- Destroying one instance doesn't affect the other

### 2. IPC protocol extension

Add instance ID to the message protocol:

- Instance ID is the first 4 bytes of every payload (included in
  payload_size)
- Bridge assigns IDs on INIT (starting from 1)
- Host stores the assigned ID per plugin instance
- MIDI_EVENT and SET_PARAM (fire-and-forget) also carry instance ID

Update `ipc.h` with helper functions for reading/writing instance-aware
messages. Maintain the same framing (`[opcode][size][payload]`) — the
instance ID is part of the payload, not the header.

Acceptance:
- Messages for different instances are correctly dispatched
- Backward-compatible framing (same header structure)

### 3. Process pool in the factory

The factory manages a pool of bridge processes:

- `BridgePool` tracks running bridge processes keyed by isolation group
  (shared: one per arch+format, per-binary: one per plugin path,
  per-instance: ephemeral)
- `create_plugin()` looks up or creates the appropriate bridge process
- `plugin->destroy()` removes the instance from its bridge; if the bridge
  has no remaining instances (per-binary/per-instance mode), kill it
- `deinit()` shuts down all bridge processes

Acceptance:
- Two instances of the same plugin reuse one bridge process in shared mode
- Per-instance override causes a separate process

### 4. Config integration

Extend `config.toml` parsing to read isolation config:

```toml
[isolation]
default = "shared"

[[isolation.override]]
match = "keepsake.vst2.58667358"
mode = "per-instance"
```

- Global default and per-plugin overrides
- Match by plugin ID (exact) or name glob
- Read at factory init, applied at `create_plugin()` time

Acceptance:
- Global default changes the isolation behaviour
- Per-plugin override correctly isolates a specific plugin

### 5. Crash handling

When a shared bridge process crashes:
- All instances hosted in it are marked crashed
- Each returns silence + CLAP_PROCESS_ERROR
- Shared memory for each instance is cleaned up

Acceptance:
- Killing the shared bridge marks all its instances as crashed
- The host doesn't crash or hang

### 6. Evidence

- bridge-test exercising multiple instances in one bridge
- Config-driven isolation override test
- Crash handling with shared process

## Stop Conditions

- Stop if serialized audio processing across instances in one bridge
  introduces unacceptable latency (measure per-instance overhead)
- Stop if the multiplexed protocol makes the audio-thread pipe I/O
  unreliable

## Evidence Requirements

- Multiple instances sharing a process, each producing correct audio
- Per-plugin isolation override working
- Crash in shared process → all instances silence, host survives

## Next Task

After this milestone: UI for isolation configuration (preferences panel
or config file documentation), or further g01 polish.
