# 006 - Process Isolation Policy

Status: active
Owner: Infinite Loop Audio
Updated: 2026-04-11
Depends on: docs/contracts/004-ipc-bridge-protocol.md
Authority owners: Infinite Loop Audio
Affects: g01.010 and all subsequent milestones that touch the bridge lifecycle

## Problem

The current bridge model spawns one subprocess per plugin instance. This is
safe but expensive — a 100-instance session means 100 processes with
duplicated memory, per-process IPC overhead, and slow instantiation. Real DAW
workflows need configurable isolation: shared processes for trusted plugins,
individual isolation for problematic ones.

## Contract

### Isolation modes

Keepsake supports three isolation modes, configurable globally and
overridable per-plugin:

| Mode | Behaviour | Default for |
|---|---|---|
| `shared` | All plugins sharing this mode load in a single long-lived bridge process per architecture | Trusted plugins (default) |
| `per-binary` | One bridge process per unique plugin binary; multiple instances of the same plugin share a process | Explicitly configured |
| `per-instance` | One bridge process per plugin instance (current model) | Blacklisted / crash-prone plugins |

### Default behaviour

- The global default is `shared` — a single bridge process hosts all plugin
  instances unless overridden.
- Separate shared processes exist per architecture (native, x86_64, 32-bit)
  and per format (VST2, VST3, AU) — so there may be up to ~6 shared
  processes in a mixed-format session, but not one per instance.
- Per-plugin overrides in `config.toml` can escalate to `per-binary` or
  `per-instance`.
- If a shared process crashes, all plugins in that process are affected.
  Keepsake reports the crash for each affected instance and may optionally
  restart the shared process.

### Configuration

```toml
[isolation]
# Global default: "shared", "per-binary", or "per-instance"
default = "shared"

# Per-plugin overrides (match by plugin ID or name glob)
[[isolation.override]]
match = "keepsake.vst2.58667358"  # Serum, by plugin ID
mode = "per-instance"

[[isolation.override]]
match = "keepsake.vst3.*"         # All VST3 plugins
mode = "per-binary"

[[isolation.override]]
match = "*Kontakt*"               # By name glob
mode = "per-instance"
```

### IPC protocol changes

The bridge subprocess becomes a **multi-instance host**. The IPC protocol
gains an instance ID field:

- Every host→bridge message (except SHUTDOWN) carries a `uint32_t instance_id`
  prepended to the existing payload.
- INIT creates a new instance (bridge allocates the ID and returns it in
  the OK response).
- SHUTDOWN with `instance_id = 0` shuts down the entire bridge process.
- SHUTDOWN with a specific `instance_id` removes that instance only.

New message framing:

```
[uint32_t opcode][uint32_t payload_size][uint32_t instance_id][payload...]
```

The instance ID is part of the payload (included in payload_size), not a
separate header field, to maintain backward compatibility with the message
framing.

### Bridge process lifecycle

- **Shared mode**: the factory spawns the shared bridge process on first
  `create_plugin()`. Subsequent instances reuse it. The process stays alive
  until `clap_entry.deinit()`.
- **Per-binary mode**: the factory spawns a bridge process on first
  `create_plugin()` for a given plugin binary. Subsequent instances of the
  same binary reuse it. The process exits when all its instances are
  destroyed.
- **Per-instance mode**: same as current behaviour — one process per
  `create_plugin()`, exits on destroy.

### Shared memory

Each instance still gets its own shared memory segment (different buffer
sizes, channel counts). The segment name includes the instance ID:
`/keepsake-<pid>-<instance_id>`.

### Crash handling in shared mode

If the shared bridge process crashes:
1. All plugin instances hosted in that process are marked crashed.
2. Each instance outputs silence and returns `CLAP_PROCESS_ERROR`.
3. The host is notified via `request_restart` for each affected instance.
4. Optional: the factory may restart the shared process and re-initialize
   surviving instances (deferred — not required for initial implementation).

### Thread safety

With multiple instances in one bridge process, the bridge must handle
interleaved messages. The host must not send messages for different instances
concurrently on the same pipe — messages are serialized. The bridge processes
them sequentially.

For audio processing, the host sends PROCESS for each instance in sequence
(not in parallel). This is acceptable because CLAP hosts typically call
`process()` on the audio thread sequentially across plugins anyway.

## Validation

- Verify that two instances of the same plugin share a bridge process in
  `shared` mode.
- Verify that two different plugins share a bridge process in `shared` mode.
- Verify that `per-instance` override causes a separate process.
- Verify that crashing the shared process marks all hosted instances as
  crashed.
- Verify that config.toml overrides are read and applied correctly.

## Roadmap Impact

- g01.010: first implementation
- All subsequent milestones that touch bridge lifecycle or instantiation

## Next Task

Implement g01.010 — multi-instance bridge and configurable isolation.
