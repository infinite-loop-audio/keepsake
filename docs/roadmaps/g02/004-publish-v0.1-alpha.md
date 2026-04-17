# G02.004 — Publish v0.1-alpha

Status: active
Owner: Infinite Loop Audio
Updated: 2026-04-17
Governing refs:
  - docs/contracts/001-working-rules.md
  - docs/project-brief.md
Auto-continuation: disallowed until g02.001-003 are complete

## Scope

Ship the first public alpha release from the now-defined support envelope and
evidence pack.

This milestone is publication only:
- tag
- artifacts
- release notes
- changelog alignment
- final release verification

It must not reopen scope decisions or introduce last-minute feature work.

Current pre-publish state:

- release notes/install surface aligned
- validation matrix aligned
- known issues aligned
- packaged artifact proof exists on the primary macOS lane
- `Release Binaries` workflow green at `24551886351`
- `effigy doctor` still reports the accepted `scan.god-files` debt from
  `g01.015`; that is not a `v0.1-alpha` publication blocker

## Steps

### 1. Cut the release candidate commit

Freeze the release candidate:

- clean worktree
- green CI
- version/changelog aligned
- docs and known issues aligned

Acceptance:
- one release candidate commit is named and reproducible

### 2. Build and attach artifacts

Generate the final alpha artifacts and checksums, then attach them to the
release.

Acceptance:
- all artifacts listed in g02.002 are present
- checksum list is published with the release

### 3. Publish the GitHub release

Publish the `v0.1-alpha` release body with:

- highlights
- supported scope
- install instructions
- known issues
- CI/evidence references where useful

Acceptance:
- public release page is complete and self-contained

### 4. Final smoke verification

Verify the published artifact, not just local build outputs:

- install from packaged artifact on the primary alpha platform
- launch host
- scan/add/load one representative plugin

Acceptance:
- one post-publication smoke pass succeeds from release artifacts

## Evidence Requirements

- release tag
- release URL
- checksum manifest
- final CI run reference
- post-publication smoke note

## Stop Conditions

- stop if release artifacts differ materially from the validated build
- stop if the final release candidate introduces new unvalidated claims

## Next Task

After publication, open a short post-alpha stabilization lane for incoming bug
reports and claim corrections, or roll directly into the next generation if the
alpha proves stable enough to widen support scope.
