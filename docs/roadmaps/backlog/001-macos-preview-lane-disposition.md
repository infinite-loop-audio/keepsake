# Backlog 001 — macOS Preview Lane Disposition

Status: backlog
Owner: Infinite Loop Audio
Created: 2026-04-17
Depends on:
  - g02.005 complete
  - stable macOS live-editor baseline
Related refs:
  - docs/architecture/macos-bridged-ui-options.md
  - docs/logs/2026-04/16-221500-mac-iosurface-embedded-ui-architecture-decision.md
  - docs/logs/2026-04/17-064500-macos-live-editor-stabilization-cleanup.md

## Problem

Keepsake now has a coherent macOS posture:

- bridge-owned live editor is the supported interaction model
- preview / IOSurface remains diagnostic-only

The remaining open question is not product behavior. It is maintenance posture:

- should the retained preview implementation stay in tree as operator-only code
- or should it move onto a later removal/simplification track once its
  diagnostic value is no longer worth the maintenance cost

## Why this is backlog, not active

This is not blocking the current alpha support claim set.

The code is already demoted behind diagnostic posture. Removing it now would be
cleanup work, not release-critical work, and should not interrupt the active
alpha packaging / validation generation without a clearer payoff.

## Scope when promoted

1. Audit the remaining preview implementation surface:
   - `src/plugin_gui_mac_embed.mm`
   - `src/plugin_gui_mac_embed.h`
   - `src/bridge_gui_mac_iosurface.mm`
   - supporting plugin/bridge call sites
2. Decide one of:
   - retain as operator-only diagnostics with explicit maintenance bounds
   - remove entirely and simplify the macOS GUI surface
3. If retained:
   - confirm the remaining diagnostics are intentional and documented
   - verify the lane does not distort release/support claims
4. If removed:
   - delete unused code and config branches
   - remove obsolete docs references
   - update known issues / architecture / roadmap surfaces accordingly

## Promotion criteria

Promote this only when one of the following becomes true:

- the preview lane starts causing real maintenance drag or regressions
- release work is sufficiently complete that cleanup becomes the highest-value
  batch
- a maintainer explicitly wants a smaller, clearer macOS GUI surface before or
  after alpha publication

## Exit criteria

- the retained-or-removed status of the macOS preview lane is explicit
- docs and code comments match that outcome
- there is no implied support ambiguity around preview vs live editor posture

## Next

Leave this deferred until the alpha release lane no longer has higher-value
support, packaging, or validation work in front of it.
