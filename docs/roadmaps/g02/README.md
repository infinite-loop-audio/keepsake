# G02 — v0.1 Alpha Release Stream

Status: active
Started: 2026-04-12

## Milestones

| Milestone | Title | Status |
|---|---|---|
| `001` | [Alpha Scope, Claims, and Docs Reconciliation](001-alpha-scope-claims-and-docs.md) | complete |
| `002` | [Release Packaging, Versioning, and Install Surface](002-release-packaging-versioning-install-surface.md) | complete |
| `003` | [Alpha Validation Matrix and Evidence Pack](003-alpha-validation-matrix-and-evidence.md) | complete |
| `004` | [Publish v0.1-alpha](004-publish-v0.1-alpha.md) | complete |
| `005` | [macOS UI Model and Interactive Fallback Prototype](005-macos-ui-model-and-fallback-prototype.md) | complete |

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

Open the post-alpha stabilization lane: bug intake, claim corrections, and
install/runtime polish against the now-published `v0.1-alpha` surface.

Current release posture outcome:
- `g02.001` effectively closed in docs posture: the alpha scope/claims baseline
  is now explicit
- `g02.002` effectively closed in release posture: version, changelog, install
  surface, release notes, and artifact naming are now aligned to the current
  support envelope
- `g02.003` effectively closed in evidence posture: packaged-artifact REAPER
  smoke is fresh on the supported macOS lane, and the release matrix/known
  issues are updated to current reality
- `g02.004` complete — `v0.1-alpha` is tagged, published, and backed by a
  post-publication packaged-artifact smoke on the primary macOS lane
- `g02.005` complete — macOS defaults to the bridge-owned live editor path;
  IOSurface preview remains diagnostic-only
