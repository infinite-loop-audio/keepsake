# Demos

Keepsake now ships a small Effigy demo stack for repo-owned proof.

List and run them with:

```sh
effigy demo list
effigy demo inspect <demo-id>
effigy demo run <demo-id>
effigy demo:supported-proof
```

If you want the normal supported stack, use:

```sh
effigy demo:supported-proof
```

## Supported Proof

These are the normal repo proof lanes for the tiny in-repo VST2 fixtures and
host harnesses.

| Demo | What it proves |
|---|---|
| `vst2-scan-smoke` | scan-path override, VST2 metadata extraction, CLAP factory exposure |
| `vst2-host-audio-smoke` | instantiate, bridge launch, activation, process loop, audio path |
| `vst2-host-threaded-smoke` | host main-thread lifecycle calls stay responsive during load/start |
| `vst2-gui-smoke` | macOS live-window editor open, scripted input path, close, bridge shutdown |
| `vst2-gui-param-state-smoke` | host-originated CLAP param flush, chunk save/load, state restore |

## Diagnostic Proof

This lane is intentionally not part of the supported normal posture.

| Demo | What it proves | Why it is diagnostic-only |
|---|---|---|
| `vst2-gui-preview-ui-state-smoke` | preview-path scripted UI click changes editor state and chunk contents | macOS preview / IOSurface is a diagnostic lane, not the supported interaction posture |

## Current Reading

- Treat `vst2-gui-smoke` as the supported macOS GUI proof.
- Treat `vst2-gui-preview-ui-state-smoke` as a narrow operator/debug proof for
  preview-path investigation.
- Do not treat the repo fixture demos as a substitute for real-host REAPER
  validation on the published lane.

## Next Task

Keep this list tight. If another supported demo gets added, decide whether it
belongs in `demo:supported-proof` or should stay opt-in.
