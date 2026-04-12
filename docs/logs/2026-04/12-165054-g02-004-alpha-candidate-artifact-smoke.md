# Release Checkpoint — v0.1-alpha Candidate Artifact Smoke

Status: updated
Date: 2026-04-12
Owner: Codex
Roadmap: g02.004
Release: v0.1-alpha

## Summary

- Built the first repeatable alpha candidate package from the repo.
- Moved publishable artifact build into GitHub Actions.
- Verified final release-artifact CI and normal CI on the current candidate commit.

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
  - CI-built artifacts now exist for macOS arm64, Linux x64, and Windows x64

## Artifact Build

- release candidate command:
  - `effigy release:candidate:alpha`
- artifacts built:
  - local dry-run:
    - `dist/v0.1-alpha/keepsake-macos-arm64-v0.1-alpha.zip`
  - CI artifacts:
    - `macos-arm64`
    - `linux-x64`
    - `windows-x64`
- helper-binary layout verified:
  - yes
- checksum manifest:
  - local dry-run:
    - `dist/v0.1-alpha/SHA256SUMS.txt`
- checksum:
  - local dry-run:
    - `359f1b77dbc0c94e4ee58f7229a3013a054f9bc4480c92884c8131209f7cf973  keepsake-macos-arm64-v0.1-alpha.zip`

## Release Notes

- changelog section:
  - `CHANGELOG.md`
- release notes surface:
  - `docs/releases/v0.1-alpha.md`
- known issues:
  - `docs/known-issues-v0.1-alpha.md`

## Publication

- release candidate commit:
  - `8d24765`
- tag:
  - not cut
- CI run:
  - release artifacts:
    - `24310750164`
    - `https://github.com/infinite-loop-audio/keepsake/actions/runs/24310750164`
  - normal CI:
    - `24310748505`
    - `https://github.com/infinite-loop-audio/keepsake/actions/runs/24310748505`
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
- Windows and Linux CI artifacts now exist, but release scope is still conservative.
- Public alpha still needs an explicit ship/no-ship decision for the experimental Windows/Linux artifacts.

## Next Task

Decide whether `v0.1-alpha` publishes macOS-only or ships the experimental Windows/Linux CI artifacts with explicit experimental wording.
