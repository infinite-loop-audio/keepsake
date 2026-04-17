# 2026-04-17 06:45 — macOS live-editor stabilization cleanup

## Summary

Closed the temporary debugging surface that accumulated during the macOS live
editor stabilization push and left the repo on a cleaner, intentional
diagnostic posture.

The important baseline from `81763f8` stands:

- REAPER-side live-editor lockups were fixed by removing the default macOS
  floating/live host-callback poll
- bridged `x64` macOS live windows keep the in-window Keepsake strap
- Khords `Full` resize is now stable on the wrapped parentless path once the
  current x64 helper is actually installed

## What changed

- removed the temporary parentless mutation-matrix controls from the normal
  bridge code path
- removed the matching parentless mutation CLI surface from `mac-clap-host`
- kept only the diagnostics that still have clear value:
  - `KEEPSAKE_MAC_FLOATING_STATE_POLL=1`
  - `KEEPSAKE_MAC_PARENTLESS_RESIZE_TRACE=1`
- updated README so the remaining macOS diagnostic hooks are explicit and the
  old matrix lane is not implied as part of the normal workflow

## Why

The parentless mutation matrix was useful for narrowing the earlier APC/REAPER
issue, but it no longer earns a place in the day-to-day surface:

- the real REAPER lockup root cause was plugin-side callback churn, not the
  individual parentless window mutations
- keeping the matrix exposed would preserve a large amount of low-signal
  toggling surface after the architectural conclusion is already clear
- the remaining diagnostics are now narrowly scoped to real unresolved classes:
  host callback comparison and parentless resize tracing

## Validation

- current macOS live-editor baseline already validated manually in REAPER for:
  - Serum
  - APC
  - Khords
- cleanup batch validation is limited to build sanity on the touched bridge and
  harness targets; this batch intentionally does not change the live-editor
  behavior itself

## Outcome

The repository now reflects the real macOS posture more cleanly:

- live editor is the supported interaction path
- preview remains diagnostic-only
- only the diagnostics that still serve active debugging remain exposed

## Next

Do one broader documentation/release hygiene pass around the current macOS
baseline, then decide whether the legacy preview implementation should remain
in tree as operator-only code or move to a later removal/simplification batch.
