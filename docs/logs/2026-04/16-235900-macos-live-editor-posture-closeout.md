# macOS Live Editor Posture Closeout

Status: active
Owner: Infinite Loop Audio
Updated: 2026-04-16
Related roadmap:
  - docs/roadmaps/g02/005-macos-ui-model-and-fallback-prototype.md
Related refs:
  - docs/architecture/macos-bridged-ui-options.md
  - docs/known-issues-v0.1-alpha.md

## Summary

This batch closes the macOS UI posture decision for the current alpha lane.

The bridge-owned live editor path is now the primary macOS editor model.
Embedded IOSurface preview remains in tree as a diagnostic/rendering surface,
but it is no longer treated as a supported interactive product direction.

## Evidence behind the posture

- Serum live editor validated in REAPER and manual use:
  - responsive interaction
  - correct plugin-driven sizing
  - frontmost open behavior fixed
- APC live editor validated in REAPER and manual use:
  - stable interaction
  - live window behavior acceptable
- Khords live editor validated in REAPER and harness:
  - root cause was parented live open on x64 helper-bridge path
  - parentless live open fixes the editor lifecycle
  - REAPER smoke pass confirmed scan/add/UI open
  - manual REAPER validation confirmed usable behavior after live-window fixes

## Result

- macOS alpha posture:
  - bridge-owned live editor is the default and supported path
- macOS preview posture:
  - IOSurface preview remains available as experimental / diagnostic only
- release messaging:
  - do not imply universal interactive embedded editor support on macOS

## Repo posture changes in this closeout

- `mac_mode` default changed to `live`
- README updated to describe `preview` as diagnostic-only
- alpha known-issues updated to reflect the live-editor posture
- g02.005 marked complete

## Follow-on cleanup

- If the preview path continues to add maintenance cost without release value,
  it should move behind a more obviously diagnostic switch or be retired from
  the user-facing config surface in a later cleanup card.

2026-04-16 follow-on note:
- preview has now been moved behind an explicit operator/debug env gate rather
  than remaining part of the normal alpha-facing config posture

## Next Task

Decide whether the macOS preview mode should remain user-selectable in alpha or
move behind a more explicit diagnostic/operator-only switch in the next cleanup
batch.
