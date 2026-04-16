# 2026-04-16 23:30 — macOS live-editor fallback contract

Status: complete
Scope: `g02.005` Batch 5.1
Refs:
- `docs/roadmaps/g02/005-macos-ui-model-and-fallback-prototype.md`
- `docs/architecture/macos-bridged-ui-options.md`

## Summary

Landed the first explicit macOS UI fallback contract instead of continuing to
rely on implicit floating-window behavior.

The plugin-side macOS GUI mode surface now recognizes:

- `preview` / `iosurface` / `embedded`
- `live` / `floating` / `windowed`
- `auto`

For this prototype lane, `auto` intentionally prefers the bridge-owned live
editor path on macOS. The existing IOSurface embed/render path remains
available as an explicit preview mode.

## Implementation

- normalized macOS UI mode parsing into explicit `preview/live/auto`
  semantics
- changed CLAP preferred GUI mode on macOS so `live`/`auto` advertise a
  floating editor path intentionally
- changed `gui_set_parent()` on macOS so live-mode sessions no longer try to
  open an embedded editor implicitly
- kept the IOSurface attach/render path intact for explicit preview mode
- updated `mac-clap-host` so it can exercise both preview and live macOS paths
  intentionally instead of always forcing embedded mode

## Validation

Build:

```sh
cmake --build build --target keepsake keepsake-bridge keepsake-bridge-x86_64 mac-clap-host
```

Harness:

```sh
./build/mac-clap-host ./build/keepsake.clap keepsake.vst2.58667358 \
  --ui-mode preview --open-ui --run-transport --ui-seconds 1 --process-blocks 48
```

Result:

- PASS
- embedded preview path still opens
- transport/audio still runs
- peak `0.331715`

Harness:

```sh
./build/mac-clap-host ./build/keepsake.clap keepsake.vst2.58667358 \
  --ui-mode live --open-ui --run-transport --ui-seconds 1 --process-blocks 48
```

Result:

- PASS
- live bridge-owned editor opens through the explicit GUI mode path
- transport/audio still runs
- peak `0.331738`

Install:

- rebuilt bundle copied to `~/Library/Audio/Plug-Ins/CLAP/keepsake.clap`

## Notes

- `effigy doctor` still reports the pre-existing broken docs link and existing
  `god-files` scan findings. Those were not changed by this batch.
- This does not settle the final alpha posture yet. It only proves the
  explicit fallback contract exists and can be exercised independently of
  REAPER-specific heuristics.

## Outcome

`g02.005` Batch 5.1 is complete.

Next: execute Batch 5.2 to expose the fallback mode clearly in normal host use
and validate REAPER session behavior with Serum, APC, and Khords.
