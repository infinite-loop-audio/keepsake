# Release Checkpoint — v0.1-alpha

Status: draft
Date: YYYY-MM-DD
Owner: <team/person>
Roadmap: g02.004
Release: v0.1-alpha

## Summary

- <short release summary>
- <why this release matters>

## Scope Freeze

- supported:
  - <lane>
- experimental:
  - <lane>

## Release Gates

- `effigy qa`
  - result: <pass/fail>
- `effigy doctor`
  - result: <pass/fail>
- `cmake --build build -j4`
  - result: <pass/fail>
- `release smoke gate`
  - command: `<command>`
  - result: <pass/fail>

## Validation Matrix Read

- validation matrix:
  - `docs/releases/v0.1-alpha-validation-matrix.md`
- primary evidence log:
  - `docs/logs/YYYY-MM/DD-HHMMSS-g02-003-...md`
- release read:
  - <supported lanes>

## Artifact Build

- artifacts built:
  - <artifact>
- helper-binary layout verified:
  - <yes/no>
- checksum manifest:
  - <path>

## Release Notes

- changelog section:
  - <path/section>
- release notes surface:
  - `docs/releases/v0.1-alpha.md`
- known issues:
  - `docs/known-issues-v0.1-alpha.md`

## Publication

- release candidate commit:
  - `<sha>`
- tag:
  - `v0.1-alpha`
- CI run:
  - <run id/link>
- GitHub release URL:
  - <url>

## Post-Package Smoke

- packaged-artifact smoke command:
  - `<command>`
- result:
  - <pass/fail>

## Risks / Follow-ups

- <remaining risk>

## Next Task

State the next release-ops action or the first post-release stabilization task.
