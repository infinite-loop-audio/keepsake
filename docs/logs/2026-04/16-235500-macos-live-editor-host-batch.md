# 2026-04-16 23:55 — macOS live-editor host batch

Status: partial
Scope: `g02.005` Batch 5.2
Refs:
- `docs/roadmaps/g02/005-macos-ui-model-and-fallback-prototype.md`
- `docs/logs/2026-04/16-233000-macos-live-editor-fallback-contract.md`

## Summary

Started the operator-facing/live-host batch for the macOS fallback lane.

This batch did three things:

- switched the local operator config from forced embedded preview to
  `mac_mode = "auto"`
- added visible live-editor labeling in the bridge-owned macOS editor window
- ran the live editor harness against the current comparison set:
  Serum, APC, Khords

## Operator Surface

Local config now points normal host use at the live fallback path by default:

```toml
[gui]
mac_mode = "auto"
mac_attach_target = "requested-parent"
```

README now explains the intended macOS split:

- `auto`: prefer live editor
- `live`: force live editor
- `preview`: force IOSurface preview

The macOS bridge-owned editor window now labels itself as `Live Editor` in the
title/header instead of looking like a generic floating/editor artifact.

## Validation

Build:

```sh
cmake --build build --target keepsake keepsake-bridge keepsake-bridge-x86_64 mac-clap-host
```

Harness:

```sh
./build/mac-clap-host ./build/keepsake.clap keepsake.vst2.58667358 \
  --ui-mode live --open-ui --run-transport --ui-seconds 1 --process-blocks 48
```

Result:

- PASS
- live editor path stable
- peak `0.331738`

Harness:

```sh
./build/mac-clap-host ./build/keepsake.clap keepsake.vst2.41706364 \
  --ui-mode live --open-ui --run-transport --ui-seconds 1 --process-blocks 48
```

Result:

- PASS
- APC live editor path stable
- peak `0.378329`

Harness:

```sh
./build/mac-clap-host ./build/keepsake.clap keepsake.vst2.4B524453 \
  --ui-mode live --open-ui --run-transport --ui-seconds 1 --process-blocks 48
```

Result:

- FAIL
- `effEditOpen` timed out after `5000ms`
- bridge stayed alive but `gui.show` failed
- plugin-side evidence suggests a local Khords content-path problem:
  `ENOENT /Library/Application Support/Loopmasters/Khords/Packs`

## Outcome

Batch 5.2 is **not complete** yet.

What is now true:

- macOS live fallback is understandable enough in config and window chrome
- Serum and APC both survive the live-editor harness path
- the live path is not yet clean across the current comparison set because
  Khords still fails during editor open on this machine

Remaining work for Batch 5.2:

- validate the new `auto` posture in REAPER with Serum and APC
- decide whether Khords is blocked by a plugin-local content install issue or a
  Keepsake live-editor bug
- narrow any remaining caveats explicitly before promoting the batch complete

Next: run the normal REAPER lane on this installed build with `mac_mode = "auto"`
and manually check Serum/APC first, then isolate whether Khords is failing
because of missing content or because the live-editor path is still broken for
that plugin.
