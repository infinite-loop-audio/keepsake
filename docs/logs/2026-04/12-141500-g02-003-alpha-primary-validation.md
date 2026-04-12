# G02.003 — Alpha Primary Validation

Status: complete
Date: 2026-04-12
Roadmap: g02.003
Release refs:
  - docs/releases/v0.1-alpha.md
  - docs/releases/v0.1-alpha-validation-matrix.md

## Summary

Fresh primary-lane release evidence is green.

Validated lane:

- macOS
- REAPER
- VST2
- APC
- Serum

Both plugins passed guarded real-REAPER smoke using the release-shaped
installed bundle flow, with UI open/close and short transport play/stop.

## Commands

```sh
tools/reaper-smoke.sh keepsake.vst2.41706364 \
  --vst-path /Library/Audio/Plug-Ins/VST/APC.vst \
  --timeout-sec 45 \
  --use-default-config \
  --sync-default-install \
  --open-ui \
  --run-transport

tools/reaper-smoke.sh keepsake.vst2.58667358 \
  --vst-path /Library/Audio/Plug-Ins/VST/Serum.vst \
  --timeout-sec 45 \
  --use-default-config \
  --sync-default-install \
  --open-ui \
  --run-transport
```

## Results

### APC

- scan found: `2533 ms`
- add FX finish: `2913 ms`
- UI open finish: `6264 ms`
- transport playing: `7820 ms`
- transport stopped: `8870 ms`
- result: `PASS`

### Serum

- scan found: `10456 ms`
- add FX finish: `10896 ms`
- UI open finish: `13664 ms`
- transport playing: `15215 ms`
- transport stopped: `16257 ms`
- result: `PASS`

## Read

- APC remains a valid regression plugin for the editor path and still passes.
- Serum remains a valid regression plugin for first-open GUI/params behavior
  and still passes.
- The primary alpha lane is strong enough to keep `macOS + REAPER + VST2` as
  the leading support claim.
- These results do not widen support claims for Windows, Linux, VST3, AU v2,
  or 32-bit paths.

## Follow-up

- keep broader lanes experimental until separate release-window evidence exists
- use this log and the matrix as the baseline for `v0.1-alpha` wording

## Next Task

Add the next release-claim lane only after it has fresh evidence strong enough
to survive publication as a supported path.
