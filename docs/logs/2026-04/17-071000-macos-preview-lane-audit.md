# 2026-04-17 07:10 — macOS preview lane audit

## Summary

Audited the remaining macOS IOSurface preview implementation after the live
editor path became the stable baseline.

Decision for now:

- keep the preview lane in tree as operator-only diagnostics
- do not treat it as active product direction
- put removal-or-retention onto an explicit backlog item instead of leaving it
  as an implied cleanup

## Why

The preview lane no longer blocks the alpha support posture:

- live editor is the supported macOS interaction path
- preview is already gated behind explicit diagnostic posture

That means immediate deletion would be cleanup work, not release-critical work.
At the same time, leaving it untracked would create maintenance ambiguity.

## What changed

- marked the preview helpers in:
  - `src/plugin_gui_mac_embed.mm`
  - `src/plugin_gui_mac_embed.h`
  as diagnostic-only in code comments
- created backlog item:
  - `docs/roadmaps/backlog/001-macos-preview-lane-disposition.md`
- updated:
  - `docs/roadmaps/backlog/README.md`
  - `docs/architecture/macos-bridged-ui-options.md`
  so the next decision on preview removal/retention is explicit

## Outcome

The repo now has a clearer posture:

- live editor: active supported path
- preview: retained only as diagnostic/operator code
- preview removal or retention: tracked backlog decision, not vague future work

## Next

Return to the active alpha release lane unless the preview code starts causing
real maintenance drag or regressions that justify promoting backlog `001`.
