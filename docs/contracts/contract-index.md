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

## Missing or Pending Contracts

| Boundary | Why needed | Blocking roadmaps | Next action |
|---|---|---|---|
| CLAP plugin factory interface | Defines descriptor shape, plugin ID namespace, factory lifecycle | g01.001 (planned) | Create once first milestone is drafted |
| VeSTige loader ABI contract | Defines which VeSTige entrypoints Keepsake calls and how | g01.001 (planned) | Create once first milestone is drafted |
| IPC bridge protocol | Defines subprocess lifecycle, crash handling, message format | g01.002+ (planned) | Create when IPC design is chosen |
| Platform config format | config.toml schema and scan path semantics | g01.001+ (planned) | Create once format is decided |

## Roadmap Readiness

No roadmap milestones exist yet. The first milestone (CLAP factory
proof-of-concept) should be drafted next and will depend on the CLAP factory
interface contract being created before execution begins.

## Next Task

Author the first roadmap milestone, then create the CLAP factory interface
contract to unblock it.
