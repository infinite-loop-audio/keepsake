# Specs

Use this folder when a change needs provisional planning before it is settled
into architecture and contracts.

## Artifact Types

- `NNN-<slug>.md` — master specs for material goals or epics
- `batch-cards/NNN-<slug>.md` — tightly scoped execution cards derived from an
  active master spec
- `batch-cards/README.md` — explains how the batch-card lane should work

## Rule

Before roadmap generation rollover, purge stale generation-specific specs and batch cards from the active tree so the next generation does not inherit dead planning debris.


Specs are a stepping stone, not the final authority.

Use specs to work through a change while the path is still being shaped. Once
the durable outcomes are accepted:

- structural decisions should be promoted into `docs/architecture/`
- behavioral or policy rules should be promoted into `docs/contracts/`

Roadmap execution should rely on architecture and contracts, not only on raw
spec text, once the change has moved out of planning.

Use an explicit lifecycle:

- `active` when the spec still governs live planning or an imminent batch
- `retired-in-place` when the lane is closed but the artifact still deserves a
  short-lived place in the active tree for traceability
- `archived` when the artifact no longer governs live work and should move out
  of the active specs surface

Prefer archive over indefinite retired-in-place clutter. `docs/specs/archive/`
is the preservation surface.

## Current Active Specs

No active specs. The first spec should be created when the first roadmap
milestone is ready to be shaped into batch cards.

## Templates

- `batch-cards/README.md`
- `archive/README.md`
- `templates/master-spec-template.md`
- `templates/batch-card-template.md`

## Next Task

Create a master spec when the first roadmap milestone is large or uncertain
enough to justify provisional planning before promotion into
architecture/contracts.
