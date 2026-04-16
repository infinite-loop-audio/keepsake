# G02 — v0.1 Alpha Release Stream

Status: planning
Started: 2026-04-12

## Milestones

| Milestone | Title | Status |
|---|---|---|
| `001` | [Alpha Scope, Claims, and Docs Reconciliation](001-alpha-scope-claims-and-docs.md) | ready |
| `002` | [Release Packaging, Versioning, and Install Surface](002-release-packaging-versioning-install-surface.md) | blocked on 001 |
| `003` | [Alpha Validation Matrix and Evidence Pack](003-alpha-validation-matrix-and-evidence.md) | blocked on 001-002 |
| `004` | [Publish v0.1-alpha](004-publish-v0.1-alpha.md) | blocked on 001-003 |
| `005` | [macOS UI Model and Interactive Fallback Prototype](005-macos-ui-model-and-fallback-prototype.md) | in_progress |

## Sequencing Intent

G02 turns the current working bridge into a releasable alpha without pretending
that every implemented code path is already proven enough to claim support.

The generation closes when:
- the supported alpha scope is explicit and honest
- public docs match real behavior and known limitations
- release artifacts and install instructions exist
- the claimed support matrix has evidence behind it
- `v0.1-alpha` ships with changelog, known issues, and reproducible artifacts

This generation is release-hardening, not feature expansion. New feature work
should stay out unless it is directly required to make the alpha claim set
true. `005` exists under that exception: the current macOS alpha story still
needs a viable editor model that does not pretend universal interactive embed.

## Release Posture

- Prefer a conservative alpha scope over broad unsupported claims.
- Treat `macOS + REAPER + VST2` as the strongest current evidence lane.
- Treat other platforms, hosts, and formats as unsupported or experimental
  until the validation matrix proves otherwise.
- Docs drift is a release blocker, not polish.

## Next Task

Execute the highest-priority ready card for the current operator intent:

- `g02.005` if the goal is to prototype the macOS editor fallback model now
- `g02.001` if the goal is to continue release-scope/doc reconciliation first

Current macOS fallback lane:
- `g02.005` Batch 5.2 — expose the fallback mode clearly in normal host use and validate REAPER session behavior with Serum, APC, and Khords
