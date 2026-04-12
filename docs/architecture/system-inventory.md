# System Inventory

Status: active
Owner: Infinite Loop Audio
Updated: 2026-04-12
Vision refs: docs/vision/001-keepsake-vision.md

## Execution-Relevant Surfaces

| Surface | Type | Status | Notes |
|---|---|---|---|
| `keepsake.clap` binary | Deliverable | Implemented | Outer CLAP plugin bundle / module |
| CLAP plugin factory | Code surface | Implemented | Exposes per-plugin descriptors across supported formats |
| VeSTige loader | Code surface | Implemented | Uses VeSTige only for VST2 |
| VST3 bridge loader | Code surface | Implemented, release proof partial | Exists in tree; alpha claim set still needs validation |
| AU v2 bridge loader | Code surface | Implemented, release proof partial | macOS only; alpha claim set still needs validation |
| Scanner + cache | Code surface | Implemented | Per-format scan with cached metadata and rescan logic |
| Out-of-process host | Code surface | Implemented | Shared/per-binary/per-instance isolation |
| IPC bridge | Code surface | Implemented | Pipe + shared-memory protocol |
| Platform config + cache files | Config surface | Implemented, docs incomplete | Runtime exists; release-grade schema docs still pending |
| Build system | Tooling | Implemented | CMake + CI across macOS, Windows, Linux |
| REAPER smoke harness | Tooling | Implemented | Guarded real-host validation lane on macOS |
| Alpha known-issues surface | Release surface | New in g02 | Governs first public alpha caveats |

## Repo Surfaces

| Surface | Owns | Notes |
|---|---|---|
| This repo (`keepsake`) | All code, docs, releases | Standalone OSS |
| Signal repo | CLAP host integration (tier 2+) | Separate, optional integration |

## Next Task

Use g02.001 to mark which implemented surfaces are actually in the
`v0.1-alpha` support envelope and which remain experimental until validated.
