# Contract Index

Status: active
Owner: Infinite Loop Audio
Updated: 2026-04-10

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
| VeSTige loader ABI contract | Defines which VeSTige entrypoints Keepsake calls and how | g01.002+ (optional) | Author as 003 if loader boundary needs stabilizing |
| Platform config format | config.toml schema and scan path semantics | g01.003+ (planned) | Create once format is decided |

## Roadmap Readiness

g01.001 is **complete**. g01.002 is **ready** — the IPC bridge protocol
contract (004) is authored and the milestone card is complete. Execution can
begin.

## Next Task

Land g01.002 — implement the bridge binary, pipe protocol, shared memory
audio transfer, and CLAP plugin lifecycle.
