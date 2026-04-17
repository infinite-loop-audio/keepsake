# 2026-04-17 07:30 — g02.001 alpha scope closeout

## Summary

Closed the remaining posture gap around `g02.001`.

The repo had already accumulated most of the alpha-scope truth across the
README, validation matrix, known issues, and the macOS live-editor decision
work, but the roadmap/front-door surfaces still lagged behind that reality.

This batch reconciled those surfaces so the release stream now reads cleanly:

- `g02.001` is effectively complete
- `g02.002` is the next active release batch
- the strongest supported alpha lane is explicit
- the macOS live-editor path is the validated interaction posture

## What changed

- updated front-door and authority surfaces:
  - `docs/README.md`
  - `docs/vision/README.md`
  - `docs/architecture/system-architecture.md`
  - `docs/architecture/system-inventory.md`
  - `docs/contracts/contract-index.md`
  - `docs/roadmaps/README.md`
  - `docs/roadmaps/g02/README.md`
- updated `README.md` to reflect the current primary plugin comparison set and
  remove stale “early stages” wording in contribution guidance
- marked `docs/roadmaps/g02/001-alpha-scope-claims-and-docs.md` as complete and
  added explicit closeout notes/evidence

## Outcome

The alpha release lane is now sequenced honestly:

- `g02.001` closed: scope, claims, and doc posture are aligned
- `g02.002` next: packaging, install surface, versioning, release-note
  scaffolding

## Next

Execute `g02.002` as the next meaningful release batch.
