# System Inventory

Status: draft
Owner: Infinite Loop Audio
Updated: 2026-04-10
Vision refs: docs/vision/001-keepsake-vision.md

## Execution-Relevant Surfaces

| Surface | Type | Status | Notes |
|---|---|---|---|
| `keepsake.clap` binary | Deliverable | Planned | Single binary, all platforms |
| CLAP plugin factory | Code surface | Planned | Exposes per-VST2-plugin descriptors |
| VeSTige loader | Code surface | Planned | Uses VeSTige header only, no Steinberg SDK |
| VST2 scanner | Code surface | Planned | Path-based scan, result cache |
| Out-of-process host | Code surface | Planned | Subprocess per plugin, crash isolation |
| IPC bridge | Code surface | Planned | Connects CLAP process to VST2 subprocess |
| Platform config file | Config surface | Planned | Per-platform path; format TBD |
| Build system | Tooling | Planned | C/C++, CMake or similar |

## Repo Surfaces

| Surface | Owns | Notes |
|---|---|---|
| This repo (`keepsake`) | All code, docs, releases | Standalone OSS |
| Signal repo | CLAP host integration (tier 2+) | Separate, optional integration |

## Next Task

Update this inventory as surfaces are implemented and their boundaries become
clearer.
