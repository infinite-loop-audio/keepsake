# G02.003 — Alpha Evidence Refresh

Date: 2026-04-17
Milestone: `g02.003`
Status: complete

## What changed

This batch refreshed the release-window evidence pack against the now-locked
alpha surface instead of the earlier draft posture.

Updated:

- `docs/releases/v0.1-alpha-validation-matrix.md`
- `docs/known-issues-v0.1-alpha.md`
- `docs/roadmaps/g02/003-alpha-validation-matrix-and-evidence.md`

## Commands and hosts used

Packaged candidate:

- `effigy release:candidate:alpha`

Primary lane smoke, using the packaged bundle and normal REAPER config/install
path:

- `tools/reaper-smoke.sh keepsake.vst2.41706364 --vst-path /Library/Audio/Plug-Ins/VST/APC.vst --clap-bundle <artifact>/keepsake.clap --use-default-config --sync-default-install --open-ui --run-transport`
- `tools/reaper-smoke.sh keepsake.vst2.58667358 --vst-path /Library/Audio/Plug-Ins/VST/Serum.vst --clap-bundle <artifact>/keepsake.clap --use-default-config --sync-default-install --open-ui --run-transport`
- `tools/reaper-smoke.sh keepsake.vst2.4B524453 --vst-path /Library/Audio/Plug-Ins/VST/Loopmasters/Khords.vst --clap-bundle <artifact>/keepsake.clap --use-default-config --sync-default-install --open-ui --run-transport`

CI reference:

- push CI `24551205423`

## Results

Packaged macOS alpha artifact:

- artifact:
  - `dist/v0.1-alpha/keepsake-macos-arm64-v0.1-alpha.zip`
- checksum:
  - `9b0a918fea3731a7fbc0210a679c428391b2d73922f29af9bf1d92fc65697f07`

Primary lane results:

- APC:
  - `PASS`
  - UI open
  - transport/audio pass
  - peak `0.380854`
- Serum:
  - `PASS`
  - UI open
  - transport/audio pass
  - peak `0.332160`
- Khords:
  - `PASS`
  - UI open
  - transport/audio pass
  - peak `0.453341`

Current CI:

- macOS: `PASS`
- Linux: `PASS`
- Windows: `PASS`

## Result

`g02.003` is now closed enough to trust:

- the supported macOS lane has fresh packaged-artifact REAPER evidence
- the release matrix is no longer anchored to pre-packaging assumptions
- the known-issues surface now matches the current release read
- Windows and Linux remain experimental in wording despite green CI and older
  exploratory host evidence

## Next Task

Execute `g02.004` — publish `v0.1-alpha` only if the release page, artifact
attachments, validation matrix, and known issues stay aligned at the tag cut.
