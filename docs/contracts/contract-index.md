# Contract Index

Status: active
Owner: Infinite Loop Audio
Updated: 2026-04-17

## Coverage Rules

- Every execution-relevant boundary should map to a contract or an explicit
  pending item below.
- Roadmap milestones must reference the governing contract ids directly.
- If a required boundary has no contract, mark the roadmap blocked and close
  the gap before execution continues.

## Contract Register

| Contract | Boundary | Owning surface | Dependent roadmaps | Status |
|---|---|---|---|---|
| `001-working-rules.md` | Execution grammar and autonomy rules | keepsake repo | all | active |
| `002-clap-factory-interface.md` | Descriptor shape, plugin ID namespace (multi-format), factory lifecycle | keepsake repo | g01.001+ | active |
| `004-ipc-bridge-protocol.md` | Subprocess lifecycle, pipe protocol, shared memory layout, crash handling | keepsake repo | g01.002+ | active |
| `006-process-isolation-policy.md` | Shared/per-binary/per-instance isolation, multi-instance bridge, config overrides | keepsake repo | g01.010+ | active |

## Missing or Pending Contracts

| Boundary | Why needed | Blocking roadmaps | Next action |
|---|---|---|---|
| VeSTige loader ABI contract | Defines which VeSTige entrypoints Keepsake calls and how | release-hardening only if loader boundary drifts again | Author as 003 only if the boundary needs stabilizing during release work |
| Platform config format | config.toml schema and scan path semantics | g02.001-g02.002 | Author once the alpha support envelope and user-facing config docs are settled |

## Roadmap Readiness

G01 is complete. G02 is active.

The release stream depends on the current contract set plus one still-thin
boundary:

- `002-clap-factory-interface.md` governs descriptor stability and IDs
- `004-ipc-bridge-protocol.md` governs bridge lifecycle and failure semantics
- `006-process-isolation-policy.md` governs shared/per-instance release
  behavior
- platform config format still needs a fuller user-facing contract before
  packaging and install docs can be treated as settled

## Next Task

Execute g02.002 — decide whether the platform config schema needs promotion
into its own contract before packaging and install docs close.
