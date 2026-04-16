# G02.005 — macOS UI Model and Interactive Fallback Prototype

Status: complete
Owner: Infinite Loop Audio
Updated: 2026-04-16
Governing refs:
  - docs/contracts/001-working-rules.md
  - docs/contracts/004-ipc-bridge-protocol.md
  - docs/architecture/system-architecture.md
  - docs/architecture/macos-bridged-ui-options.md
  - docs/roadmaps/g01/017-iosurface-embedded-editors.md
  - docs/logs/2026-04/16-221500-mac-iosurface-embedded-ui-architecture-decision.md
Auto-continuation: allowed within g02 once the chosen batch lands cleanly

## Scope

Turn the macOS UI architecture cutoff into a bounded prototype lane.

The current cross-process embedded-input model has a useful rendering baseline
but has failed to deliver universal reliable interaction for bridged editors,
especially JUCE-based UIs. The next practical alpha move is not more AppKit
event tweaking. It is to prototype a release-credible fallback model that keeps
macOS usable while preserving the rendering work already completed.

This milestone began as **render-only embed plus explicit interactive remote
editor fallback**, but the result is now clearer: the bridge-owned live editor
window is the macOS primary path, while embedded preview is retained only as a
diagnostic surface.

## Goals

- [x] Preserve the current IOSurface rendering baseline as best-effort embed on
      macOS.
- [x] Add an explicit macOS fallback path that opens the bridge-owned live
      editor window for real interaction.
- [x] Make the fallback controllable and understandable enough to validate in
      the current REAPER/macOS lane.
- [x] Produce evidence that this model is either viable for alpha or not worth
      carrying forward.

## Non-Goals

- [x] Do not resume generic embedded-input synthesis experiments in this
      milestone.
- [x] Do not claim universal true embedded interaction on macOS.
- [x] Do not broaden support claims for JUCE/VSTGUI/AU classes beyond the
      evidence gathered here.

## Contract Coverage

- [x] The macOS UI fallback behavior is covered by existing GUI/IPC seams or
      the new contract delta is documented before execution depends on it.
- [x] Release-posture implications are reflected in known-issues / alpha scope
      surfaces if the prototype changes claims.
- [x] This milestone remains single-repo and does not require cross-repo
      authority mapping.

## Execution Readiness

- [x] The prototype is bounded to one chosen macOS fallback model:
      bridge-owned live editor as primary path, render-only preview as a
      retained diagnostic lane.
- [x] Each execution batch below includes acceptance checks, evidence, and stop
      conditions.
- [x] Auto-continuation is allowed only while the prototype still fits the
      chosen model and does not open a new architecture branch.

## Execution Plan

### Batch 5.1 — Explicit macOS live-editor fallback contract

- [x] Define the runtime/config surface for macOS editor mode selection:
      embedded preview, live remote editor, or automatic fallback.
- [x] Implement the host/bridge control path that opens the bridge-owned live
      editor window explicitly from the plugin side without relying on the old
      floating-window heuristics.
- [x] Keep the current IOSurface rendering lane intact as a separate visual
      path rather than deleting it.

Acceptance:

- [x] macOS can open the bridge-owned live editor intentionally and repeatedly.
- [x] The live editor path does not depend on REAPER-specific hacks.
- [x] Existing embedded rendering still works as before when selected.

### Batch 5.2 — Host-visible affordance and session behavior

- [x] Expose the chosen fallback mode in config and document it clearly.
- [x] Ensure transport/audio behavior remains stable while the live editor is
      open.
- [x] Verify focus/ownership behavior in the primary lane:
      macOS + REAPER + VST2.

Acceptance:

- [x] Users can select or understand the macOS editor mode.
- [x] The fallback path is stable enough for APC / Khords / Serum manual use.
- [x] Any remaining caveats are explicit and narrow.

### Batch 5.3 — Alpha posture and evidence closeout

- [x] Update `docs/known-issues-v0.1-alpha.md` with the macOS editor model
      actually supported after the prototype.
- [x] Record a dated evidence log covering the fallback behavior versus the old
      embedded-input expectations.
- [x] Decide whether the alpha should:
      - retain preview as diagnostic-only evidence, and
      - use the live editor as the macOS editor posture.

Acceptance:

- [x] The release posture matches the actual prototype result.
- [x] No doc surface still implies universal interactive macOS embed.

## Risks and Mitigations

- Risk: the fallback path feels too clumsy for alpha usability.
- Mitigation: keep the evaluation explicit; if it fails, narrow claims rather
  than hiding the problem.

- Risk: REAPER-specific behavior still dominates the result.
- Mitigation: preserve the harness as a fast local check, but treat REAPER as
  the real host validation lane.

- Risk: retaining both embed rendering and live editor paths adds complexity.
- Mitigation: keep the prototype scoped to a single clear fallback model and
  defer broader cleanup until the model is accepted.

## Planning Gaps

- Whether preview should remain user-selectable in alpha or move behind a more
  obviously diagnostic switch later.

## Evidence Requirements

- [x] one dated log for the prototype implementation batch
- [x] one dated log for the host-validation batch
- [x] manual validation in REAPER with Serum, APC, and Khords
- [x] harness validation retained as fast side evidence, not as sole proof
- [x] explicit PASS/FAIL posture for:
      - embedded preview remains diagnostic-only, not a supported interactive mode
      - live remote editor interaction mode is the macOS alpha posture
- [x] preview no longer needs to remain part of the normal alpha config surface

## Stop Conditions

- stop if the live-editor fallback requires a new architecture decision not
  covered by `docs/architecture/macos-bridged-ui-options.md`
- stop if the prototype cannot be made operator-usable enough to support an
  honest alpha claim
- stop if REAPER/host validation disproves the fallback as a viable macOS UI
  direction

## Next Task

Follow with a narrower cleanup card only if the remaining diagnostic preview
lane should be removed entirely instead of merely hidden behind operator-only
controls.
