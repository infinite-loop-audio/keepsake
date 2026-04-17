# Changelog

## [Unreleased]

### Added
- Initial Northstar + Effigy repo contract baseline.
- Docs skeleton: vision, architecture, contracts, roadmaps, logs, specs.
- Stricter delivery spine: working rules, product guardrails, contract index,
  batch-card surfaces, and spec templates.

## [v0.1-alpha] - 2026-04-17

First public alpha release posture for Keepsake.

### Highlights
- Primary validated lane: macOS + REAPER + VST2.
- Bridged VST2 plugins expose as distinct CLAP entries with their own names,
  vendors, and metadata.
- macOS now defaults to the bridge-owned live editor path; diagnostic preview
  remains available but is no longer part of the normal support claim.
- Real-plugin REAPER validation on macOS covers Serum, APC, and Khords.
- Cross-platform CI is green on macOS, Linux, and Windows.

### Added
- Release candidate packaging flow via `effigy release:candidate:alpha`.
- GitHub Actions release packaging workflow for macOS, Linux, and Windows
  artifacts.
- Draft release notes, validation matrix, and publish checklist for
  `v0.1-alpha`.

### Changed
- Alpha support posture is now explicit and conservative:
  - supported: macOS + REAPER + VST2
  - experimental: Windows, Linux, VST3, AU v2, 32-bit
- macOS bridged editor posture now uses live editor windows as the supported
  model.

### Fixed
- Hardened bridge lifecycle around IPC corruption, stuck init, and oversized
  shared-memory allocation failures.
- Stabilized macOS live editor behavior in REAPER, including bridged x64 UI
  handling for APC and Khords.
- Fixed Linux and Windows CI build failures caused by non-mac bridge stub drift.
