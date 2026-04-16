# 2026-04-16 23:59:50 — macOS x64 live-window strap closeout

## Summary

Closed the remaining usability gap on the macOS live-editor posture for bridged
`x64` VST2 windows:

- parentless live windows now mount the Keepsake strap **inside the same
  window** above the plugin UI
- the harness now exits cleanly when those parentless live windows are closed
- the stable x64 path was preserved without returning to the broken embedded
  interaction model

This batch was validated in the harness after the REAPER live baseline had
already been proven for Serum, APC, and Khords.

## What Changed

- wrapped parentless macOS live windows in an owned outer content view so the
  existing Keepsake header/strap could sit above the plugin-owned editor
  content inside the same `NSWindow`
- kept the parentless selection/close/clamp behavior already proven in the
  REAPER lane
- updated the mac harness to poll `on_main_thread()` even in parentless live
  mode so external live-window closes are observed and the harness terminates
  cleanly

## Validation

Harness validation on the final stable build:

- APC:
  - live window renders normally
  - Keepsake strap appears inside the same window above the plugin UI
  - closing the live window terminates the harness host correctly
- Khords:
  - live window renders normally
  - Keepsake strap appears inside the same window above the plugin UI
  - closing the live window terminates the harness host correctly

## Release Meaning

The primary macOS alpha posture remains the live editor model, but this removes
an important parity gap between:

- native/owned live windows, and
- bridged `x64` parentless live windows

The embedded preview path remains diagnostic-only and is not part of this
interaction baseline.

## Next Task

Reflect this final macOS live baseline in the release validation matrix and
known-issues surfaces, then decide whether the diagnostic preview code still
earns its maintenance cost.
