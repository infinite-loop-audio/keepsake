# G02.004 — Release Artifact Proof

Date: 2026-04-17
Milestone: `g02.004`
Status: updated

## What changed

This batch did the first real publication-proof pass without cutting the tag:

- triggered `Release Binaries` for `v0.1-alpha`
- confirmed the workflow gates and all three platform packaging jobs are green
- downloaded the packaged artifacts and recorded the exact filenames and
  checksums
- updated the release notes and publish checklist to use the real artifact run
  instead of placeholders

## Release artifact run

- workflow: `Release Binaries`
- run: `24551886351`
- head SHA: `3b3c9cf4d172a35ccc1896d21a1b73ea3db34eda`
- URL:
  - `https://github.com/infinite-loop-audio/keepsake/actions/runs/24551886351`

## Artifact set

- macOS:
  - `keepsake-macos-arm64-v0.1-alpha.zip`
  - `77e0173c9f1f87726a87a29c70fcc48d32d5f3ab81217213f839eb27c8dfce9f`
- Linux:
  - `keepsake-linux-x64-v0.1-alpha.tar.gz`
  - `e59d1f3c95f3a0f68a76ddbea7a25a303cd1abf8d6f288bf5f4ccff5f68fafd1`
- Windows:
  - `keepsake-windows-x64-v0.1-alpha.zip`
  - `799835506f0c4bb9fac3597d06e211dda195502e9d04de76dfbecc02f163fb23`

## Publication read

Current recommended public artifact scope:

- ship macOS as the primary supported alpha artifact
- ship Windows and Linux only as explicitly experimental attachments

That matches the current matrix and known-issues posture. This batch did not
cut the tag or publish the GitHub release.

## Next Task

Treat this as the pre-publication proof note behind the now-published release,
and use the release checkpoint log as the current authority:

- `docs/logs/2026-04/17-080000-v0.1-alpha-release-checkpoint.md`
