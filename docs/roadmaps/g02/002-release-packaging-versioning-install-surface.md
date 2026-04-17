# G02.002 — Release Packaging, Versioning, and Install Surface

Status: complete
Owner: Infinite Loop Audio
Updated: 2026-04-17
Governing refs:
  - docs/contracts/001-working-rules.md
  - docs/project-brief.md
Auto-continuation: allowed within g02 once g02.001 is complete

## Scope

Turn the alpha scope into shippable release surfaces:

- version number and changelog
- release artifacts per platform
- install instructions
- release notes skeleton
- signing/notarization or unsigned-binary posture

This milestone is about packaging the current product honestly and
reproducibly. It should not broaden the support envelope.

## Steps

### 1. Version and changelog

Set up the first real release version surfaces:

- `CMakeLists.txt` project version
- `CHANGELOG.md`
- release tag target: `v0.1-alpha`

Acceptance:
- one canonical version string exists across build and release docs
- changelog has a real `v0.1-alpha` entry with user-visible highlights and
  caveats

### 2. Artifact definition

Define exactly what ships:

- macOS artifact shape
- Windows artifact shape
- Linux artifact shape
- helper binary placement and expectations
- checksum generation

Acceptance:
- release artifact list is written down
- artifact names and layouts are stable enough to script or repeat manually

### 3. Install and upgrade docs

Write install docs that match the artifact reality:

- download/install path per platform
- how the helper binaries are expected to sit relative to the `.clap`
- host rescan steps
- unsigned binary / Gatekeeper notes if applicable
- update/replace flow from source builds or prior alpha drops

Acceptance:
- a user can install the alpha from docs alone
- install docs mention any security/signing workaround required for alpha

### 4. Release note scaffolding

Prepare the publication surfaces:

- GitHub release body
- highlights
- supported scope
- known issues
- checksums / artifact list

Acceptance:
- release notes exist in draft-ready form before the publish milestone

## Evidence Requirements

- draft artifact manifest
- draft install doc
- draft release notes
- version and changelog diffs
- one dry run of the packaging flow on at least the primary alpha platform

## Closeout

Closed by:

- `CHANGELOG.md` gaining a real `v0.1-alpha` entry
- `docs/releases/v0.1-alpha.md` being updated to the actual release/install
  surface and current support posture
- local dry-run packaging via `effigy release:candidate:alpha`
- current push CI green across macOS, Linux, and Windows at `24551205423`

## Stop Conditions

- stop if packaging requires a broader support claim than g02.001 allowed
- stop if missing build/release automation makes artifact reproducibility too
  vague to trust

## Next Task

Execute g02.003 — run the alpha validation matrix against the claimed support
surface and collect the evidence pack needed for publication.
