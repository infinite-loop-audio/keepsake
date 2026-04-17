# Demo Proof Stack And Operator Shortcut

Date: 2026-04-17
Status: active

## What changed

Added a small repo-owned demo proof stack through Effigy plus one operator
shortcut for the normal supported path.

The stack now includes:

- `vst2-scan-smoke`
- `vst2-host-audio-smoke`
- `vst2-host-threaded-smoke`
- `vst2-gui-smoke`
- `vst2-gui-param-state-smoke`
- `vst2-gui-preview-ui-state-smoke`

Added task:

- `effigy demo:supported-proof`

Added docs front door:

- `docs/demos.md`

## Posture

Supported repo proof:

- `vst2-scan-smoke`
- `vst2-host-audio-smoke`
- `vst2-host-threaded-smoke`
- `vst2-gui-smoke`
- `vst2-gui-param-state-smoke`

Diagnostic-only repo proof:

- `vst2-gui-preview-ui-state-smoke`

This keeps the supported macOS GUI proof on the live-window posture while still
retaining one bounded operator/debug lane for preview-path investigation.

## Evidence

The supported shortcut now runs cleanly:

```sh
effigy demo:supported-proof
```

The diagnostic preview-state lane also runs cleanly when invoked directly:

```sh
effigy demo run vst2-gui-preview-ui-state-smoke
```

The current harness/demo proof now covers:

- scan-path override and CLAP descriptor exposure
- instantiate, bridge launch, activation, and audio path
- host main-thread safety during load/start
- macOS live-window editor open/close
- host-originated param/state round-trip
- preview-path UI-originated state change for diagnostic work

## Why this batch matters

Before this batch, the repo had useful harnesses but no compact proof surface.
Operators had to remember tool names and argument shapes.

Now the repo has:

- one visible demo inventory
- one supported-stack shortcut
- one explicit supported-vs-diagnostic split

That makes local proof faster without confusing repo-fixture proof with the
real-host published lane.

## Next Task

If this proof stack starts gating release or stabilization work, add one small
release-facing note that says which demo lanes are repo-fixture proof only and
which claims still require real-host REAPER evidence.
