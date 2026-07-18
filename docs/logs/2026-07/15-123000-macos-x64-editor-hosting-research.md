# 2026-07-15 12:30 — macOS x64 editor hosting research

## Summary

Tested two replacement architectures for Soundcheck-style `x86_64` VST2
inspection on Apple Silicon.

- CARemote transport works across `arm64` → `x86_64` processes, but the remote
  layer renders black.
- A disposable `x86_64` process that loads the VST2 and owns its `NSView`
  locally renders correctly.

The local inspection host is the viable next lane. CARemote is stopped unless
new evidence first fixes the black in-process control.

## Loader fixes

- stop treating the `effEditOpen` return as a Boolean
- validate the resulting AppKit child view/window instead
- keep hosted editor construction on the bridge AppKit main thread
- run `effOpen` before native metadata queries
- close libraries on entry/AEffect validation failures
- include `dlopen` / `LoadLibrary` failure detail

The repo demo VST2 now deliberately attaches its editor and returns zero from
`effEditOpen`. The normal GUI smoke passes with that behavior.

## CARemote result

The proof uses public QuartzCore APIs and a small raw Mach rendezvous:

1. `arm64` host creates `CARemoteLayerServer`.
2. Host transfers the server send right to an `x86_64` Rosetta child.
3. Child creates `CARemoteLayerClient`, assigns an animated layer tree, and
   returns the client id.
4. Host attaches `layerWithRemoteClientId:` to its window.

Receipts confirmed the request, port transfer, client creation, and host
attachment. Window and full-display screenshots showed an empty black content
area. An in-process control produced the same black result. `CATransaction`
flushes and delayed attachment did not change it.

Result: transport proof only. No production GUI integration.

## Co-located x64 result

Added an `x86_64` local VST2 host and matching `x86_64` demo plugin fixture.
The host:

- runs under Rosetta
- loads through Keepsake's VeSTige loader
- creates the editor on its AppKit main thread
- attaches the plugin view to its own host-provided `NSView`
- runs editor idle at 60 Hz

Receipt:

```text
effEditOpen end result=0
local-vst2-host: loaded arch=x86_64 window=40650 size=320x160 children=1
```

The window screenshot contained the complete editor and controls. This is the
correct process shape for Soundcheck because its inspection helper is already
disposable per plugin.

An initial cleanup attempt called `effClose`, unloaded the Objective-C plugin,
then drained host autorelease pools. The helper crashed in `objc_release`
because the plugin had left AppKit objects alive. The corrected disposable
path calls `effEditClose` and exits the process without unloading the bundle.
The OS reclaims the address space; the automated helper run then exits zero.

## Decision

- Keep bridge-owned live windows as the general Keepsake/DAW posture.
- Keep IOSurface diagnostic-only.
- Do not integrate CARemote on the current evidence.
- Build Soundcheck inspection around a Keepsake-owned disposable `x86_64`
  helper that hosts the VST2 editor locally.
- Define the helper protocol before product integration: plugin identity,
  readiness, screenshot output, timeout, crash receipt, and shutdown.
- Treat process exit—not in-process bundle unload—as the final cleanup boundary.

## Validation

- `effigy demo:build:vst2-gui-stack`
- `effigy demo run vst2-gui-smoke`
- `effigy demo:build:mac-remote-layer-proof`
- `effigy demo:build:mac-vst2-local-host`
- manual window screenshot of the `x86_64` local host

## Next Task

Define and implement the disposable inspection-helper protocol, then let the
Soundcheck thread consume that helper without duplicating VST2 hosting code.
