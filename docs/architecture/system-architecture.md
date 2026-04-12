# System Architecture

Status: active
Owner: Infinite Loop Audio
Updated: 2026-04-12
Vision refs: docs/vision/001-keepsake-vision.md

## Overview

Keepsake is a single `.clap` binary that exposes a CLAP plugin factory. The
factory returns one plugin descriptor per discovered legacy plugin — VST2,
VST3, and AU v2 — exposing each as a distinct named CLAP plugin.

Plugins are loaded via format-specific loaders (VeSTige for VST2, VST3 SDK for
VST3, AudioToolbox for AU v2) running in isolated subprocesses. The subprocess
model provides both crash isolation and bitness bridging — 32-bit plugins run
in a 32-bit helper process while the host stays 64-bit.

The architecture is now ahead of the first public release posture. For
`v0.1-alpha`, support claims should be based on fresh validation, not on the
mere existence of code paths.

## Component Layout

```
keepsake.clap
  ├─ CLAP plugin factory
  │    Returns one descriptor per discovered legacy plugin.
  │    Descriptor carries name, vendor, version, feature tags.
  │    Plugin IDs follow: keepsake.<format>.<uid>
  │
  ├─ Format loaders
  │    ├─ VST2 loader (VeSTige ABI, LGPL v2.1)
  │    ├─ VST3 loader (VST3 SDK, GPLv3 — subprocess-isolated)
  │    └─ AU v2 loader (AudioToolbox — macOS only)
  │
  ├─ Plugin scanner / cache
  │    Scans configured paths per format at startup.
  │    Caches results so factory responds immediately.
  │    Rescan triggerable via preferences or config file.
  │
  └─ Out-of-process bridge
       Configurable isolation: shared, per-binary, or per-instance.
       Default (shared): one bridge process hosts many plugin instances.
       Crash isolation: crashed process → silence + error for all hosted
         instances. Per-instance mode isolates crash-prone plugins.
       Bitness bridging: helper binaries selected by plugin architecture.
       IPC bridge between CLAP main process and loader subprocess(es).

keepsake-bridge-64 (helper binary, 64-bit)
  └─ Hosts 64-bit plugins in an isolated process.

keepsake-bridge-32 (helper binary, 32-bit — where platform supports it)
  └─ Hosts 32-bit plugins, bridging to the 64-bit main process.
```

## Key Seams

| Seam | Surface | Notes |
|---|---|---|
| VST2 ABI | VeSTige header (LGPL v2.1) | No Steinberg SDK. Clean-room only. |
| VST3 ABI | VST3 SDK (GPLv3 or proprietary) | Runs in subprocess; license boundary at process/IPC edge |
| AU v2 ABI | AudioToolbox (macOS system framework) | macOS only. No special licensing. |
| CLAP plugin interface | CLAP SDK (MIT) | Outer format. No VST3 licence conflicts. |
| IPC / subprocess model | Pipe protocol + shared memory | Governed by `docs/contracts/004-ipc-bridge-protocol.md`; current implementation also multiplexes instances inside shared bridges |
| Bridge helper binaries | `keepsake-bridge`, `keepsake-bridge-x86_64`, future `keepsake-bridge-32` | Native helper plus cross-arch helper where needed; 32-bit still needs release-grade proof before claiming support |
| Scan path config | config + cache files per platform | Runtime exists; user-facing schema/docs still need alpha release hardening |

## Platform Notes

- macOS: x86_64 + arm64. Rosetta 2 required for x86_64 plugins on Apple
  Silicon. 32-bit plugins are not supported on macOS 10.15+ (Catalina dropped
  32-bit entirely) — this is a platform limitation.
- Windows: x86_64. 32-bit plugins supported via WoW64 and the 32-bit bridge
  helper binary.
- Linux: x86_64. 32-bit plugins supported via multilib and the 32-bit bridge
  helper binary.

## Prior Art

- LMMS VeSTige integration — 20+ year reference for VeSTige usage
- Carla — closest architectural reference for multi-format plugin factory model
- Ardour — VeSTige-lineage reference for VST2 hosting

## Next Task

Use g02.001 to align this architecture doc with the alpha support envelope so
the public release posture distinguishes implemented structure from validated
support claims.
