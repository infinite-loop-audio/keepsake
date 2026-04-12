# Release Checkpoint — v0.1-alpha Candidate Artifact Smoke

Status: recorded
Date: 2026-04-12
Owner: Codex
Roadmap: g02.004
Release: v0.1-alpha

## Summary

- Built the first repeatable alpha candidate package from the current repo state.
- Verified the packaged macOS artifact through a guarded real-REAPER smoke on the primary lane.

## Scope Freeze

- supported:
  - macOS + REAPER + VST2
- experimental:
  - Windows
  - Linux
  - VST3
  - AU v2
  - 32-bit paths

## Release Gates

- `effigy qa`
  - result: pass
- `effigy doctor`
  - result: pass
- `cmake --build build -j4`
  - result: pass
- `release smoke gate`
  - command: `./build/host-test-threaded ./build/keepsake.clap keepsake.vst2.41706364 --timeout-ms 500 --entry-timeout-ms 5000 --max-memory-mb 2048 --vst-path /Library/Audio/Plug-Ins/VST/APC.vst`
  - result: pass

## Validation Matrix Read

- validation matrix:
  - `docs/releases/v0.1-alpha-validation-matrix.md`
- primary evidence log:
  - `docs/logs/2026-04/12-141500-g02-003-alpha-primary-validation.md`
- release read:
  - packaged macOS alpha candidate still matches the supported lane

## Artifact Build

- release candidate command:
  - `effigy release:candidate:alpha`
- artifacts built:
  - `dist/v0.1-alpha/keepsake-macos-universal-v0.1-alpha.zip`
- helper-binary layout verified:
  - yes
- checksum manifest:
  - `dist/v0.1-alpha/SHA256SUMS.txt`
- checksum:
  - `0da5c342315ea21b2ab8f836883064275464616cb892aaa578668812ecfc1bd8  keepsake-macos-universal-v0.1-alpha.zip`

## Release Notes

- changelog section:
  - `CHANGELOG.md`
- release notes surface:
  - `docs/releases/v0.1-alpha.md`
- known issues:
  - `docs/known-issues-v0.1-alpha.md`

## Publication

- release candidate commit:
  - `6fb8197`
- tag:
  - not cut
- CI run:
  - not refreshed in this checkpoint
- GitHub release URL:
  - not published

## Post-Package Smoke

- packaged-artifact smoke command:
  - `tools/reaper-smoke.sh keepsake.vst2.41706364 --clap-bundle "<unzipped>/keepsake.clap" --vst-path /Library/Audio/Plug-Ins/VST/APC.vst --timeout-sec 45 --open-ui --run-transport --use-default-config --sync-default-install`
- result:
  - pass
- key timings:
  - scan found: `5253 ms`
  - add FX finish: `5730 ms`
  - UI open finish: `8830 ms`
  - transport playing: `10391 ms`
  - transport stopped: `11441 ms`

## Risks / Follow-ups

- Packaged smoke from an isolated temp CLAP path timed out before REAPER ran the script.
- Realistic installed-bundle smoke passed, so the blocker is in the isolated packaged REAPER launch shape, not in the packaged bundle itself.

## Next Task

Refresh CI on the release-candidate commit, then decide whether to cut `v0.1-alpha` from the current conservative scope or widen evidence first.
