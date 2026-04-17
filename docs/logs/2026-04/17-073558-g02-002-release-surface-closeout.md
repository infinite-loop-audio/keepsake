# G02.002 — Release Surface Closeout

Date: 2026-04-17
Milestone: `g02.002`
Status: complete

## What changed

This batch turned the alpha packaging lane into a real release surface instead
of a draft:

- `CHANGELOG.md` now has a real `v0.1-alpha` entry
- `docs/releases/v0.1-alpha.md` now matches the current support claim,
  artifact naming, helper layout, install flow, and release-note posture
- `docs/roadmaps/g02/README.md` and
  `docs/roadmaps/g02/002-release-packaging-versioning-install-surface.md` now
  mark `g02.002` complete and move the lane to `g02.003`
- `tools/release-candidate.sh` now produces deterministic output by clearing
  the target dist directory before packaging, and its macOS artifact shape now
  matches the documented bundle-only helper posture

## Evidence

- local dry-run package:
  - `effigy release:candidate:alpha`
- dry-run artifact:
  - `dist/v0.1-alpha/keepsake-macos-arm64-v0.1-alpha.zip`
- dry-run checksum:
  - `9b0a918fea3731a7fbc0210a679c428391b2d73922f29af9bf1d92fc65697f07`
- docs QA:
  - `effigy qa:docs`
- current push CI:
  - `24551205423`
  - `build-macos` green
  - `build-linux` green
  - `build-windows` green

## Result

`g02.002` is now closed enough to trust:

- version/changelog surface exists
- release notes/install surface exists
- artifact naming is stable
- local candidate packaging is reproducible on the primary macOS lane
- the docs now point to the next real release task instead of stale packaging
  work

Windows and Linux artifacts remain experimental in wording. This batch did not
promote them into the supported claim set.

## Next Task

Execute `g02.003` — refresh the alpha validation matrix and evidence pack
against the now-locked release surface, not the earlier draft posture.
