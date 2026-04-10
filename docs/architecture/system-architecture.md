# System Architecture

Status: draft
Owner: Infinite Loop Audio
Updated: 2026-04-10
Vision refs: docs/vision/001-keepsake-vision.md

## Overview

Keepsake is a single `.clap` binary that exposes a CLAP plugin factory. The
factory returns one plugin descriptor per discovered VST2 plugin, exposing each
as a distinct named CLAP plugin.

VST2 plugins are loaded via the VeSTige clean-room ABI header. Each plugin
instance runs in an isolated subprocess.

## Component Layout

```
keepsake.clap
  ├─ CLAP plugin factory
  │    Returns one descriptor per discovered VST2 plugin.
  │    Descriptor carries name, vendor, version, feature tags.
  │    Plugin IDs follow: keepsake.vst2.<uid>
  │
  ├─ VST2 scanner / cache
  │    Scans configured scan paths at startup.
  │    Caches results so factory responds immediately.
  │    Rescan triggerable via preferences or config file.
  │
  └─ Out-of-process VST2 host
       Loads each VST2 plugin in a subprocess.
       Crash isolation: crashed plugin → silence + error state.
       IPC bridge between CLAP main process and VST2 subprocess.
```

## Key Seams

| Seam | Surface | Notes |
|---|---|---|
| VST2 ABI | VeSTige header (LGPL v2.1) | No Steinberg SDK. Clean-room only. |
| CLAP plugin interface | CLAP SDK (MIT) | Outer format. No VST3 conflicts. |
| IPC / subprocess model | TBD | Study Carla for reference |
| Scan path config | config.toml per platform | Format to be documented once stable |

## Platform Notes

- macOS: x86_64 + arm64. Rosetta 2 required for x86_64 VST2 on Apple Silicon.
  This is a VST2 limitation, documented as such.
- Windows: x86_64.
- Linux: x86_64.

## Prior Art

- LMMS VeSTige integration — 20+ year reference for VeSTige usage
- Carla — closest architectural reference for multi-format plugin factory model
- Ardour — VeSTige-lineage reference for VST2 hosting

## Next Task

Expand this document once the first CLAP factory prototype and VeSTige loader
are implemented and the IPC mechanism is chosen.
