# G01.017 — IOSurface Embedded Editors (macOS)

Status: parked
Owner: Infinite Loop Audio
Updated: 2026-04-16
Auto-continuation: allowed within g01

## Scope

Replace floating editor windows on macOS with true embedded editors using
IOSurface for cross-process GPU surface sharing. The plugin editor renders
in the bridge subprocess, the pixels appear in the host's plugin window
via shared GPU memory.

## Architecture

```
REAPER (host process)                    keepsake-bridge (subprocess)
  │                                        │
  │ NSView (host's plugin area)            │ NSView (offscreen, IOSurface-backed)
  │   └─ CALayer ←──── IOSurface ────────→ │   └─ Plugin editor draws here
  │                  (shared GPU memory)   │
  │                                        │
  │ Mouse/keyboard ──── IPC pipe ────────→ │ Inject events into editor
```

## Steps

### 1. Bridge: IOSurface-backed editor

- Create an IOSurface with the editor dimensions
- Create an NSView backed by a CALayer using this IOSurface
- Open the plugin editor into this view (effEditOpen)
- Send the IOSurfaceID (uint32_t) back to the host via IPC
- effEditIdle causes the plugin to render into the IOSurface

### 2. Host: composite IOSurface into host view

- Receive IOSurfaceID from bridge
- Look up the IOSurface via IOSurfaceLookup(surfaceID)
- Create a CALayer with contents = IOSurface
- Add the layer to the host's NSView (from set_parent)
- The plugin's rendering appears in REAPER's window

### 3. Event forwarding

- Host captures mouse events on its NSView
- Forward mouse position, button state, scroll via IPC
- Bridge injects NSEvents into the plugin's editor view
- Keyboard: forward key events similarly

### 4. Resize handling

- When plugin editor resizes, bridge resizes the IOSurface
- Sends new IOSurfaceID to host (or resizes in-place if possible)
- Host updates its CALayer

## Status Notes

Initial implementation attempted 2026-04-11. Three capture approaches tried:
- `displayRectIgnoringOpacity:inContext:` — partial/glitchy, misses GPU content
- `CALayer renderInContext:` — same issues
- `CGWindowListCreateImage` — obsoleted in macOS 15 SDK, captured desktop when
  window positioned offscreen

The IOSurface sharing itself works (surface created, ID passed between
processes, host CALayer displays it). The unsolved problem is reliably
capturing the plugin's rendered content INTO the surface. Most VST2 plugins
use a mix of CoreGraphics, OpenGL, and Core Animation that none of the
capture APIs handle completely.

2026-04-16 update:

- Rendering/crop alignment is materially better than this original note
  implies. The bridge now detects descendant content bounds and exports the
  correct visible rect for APC/Khords/Serum-class editors.
- Serum-class editors can render and interact partially in embedded mode, but
  note-active animated patches still flicker under the current cross-process
  model.
- JUCE-based bridged editors (APC, Khords) still fail the universal embedded
  interaction bar. Clicks reach the JUCE view, but no meaningful plugin-side
  edit callbacks follow.
- Tried and exhausted during this thread:
  - requested-parent versus broader host attach targets
  - responder-chain resolution changes
  - `NSPanel` versus `NSWindow`
  - offscreen versus visible bridge host windows
  - `NSEvent` mouse synthesis
  - `CGEvent` mouse synthesis

Conclusion:

The current "cross-process embedded bitmap plus injected native input" model
is not a dependable universal macOS editor architecture for arbitrary bridged
plugin UIs. Keep the IOSurface lane as an experimental rendering baseline, but
do not treat incremental AppKit event tweaking as the main path forward.

Promising avenues to revisit:
- **In-process GUI loading** (Option A from earlier analysis) — load the
  plugin binary in-process just for the editor, use out-of-process for audio.
  Avoids cross-process rendering entirely. Trade-off: GUI crash can affect host.
- **ScreenCaptureKit** (macOS 13+) — async screen capture API, may handle
  GPU content better than CGWindowListCreateImage. Needs investigation for
  per-window capture at 60fps.
- **CARemoteLayer** — Apple's internal mechanism for AUv3 hosting. Not public
  API but used by Logic, GarageBand, and others. May be viable with careful use.
- Wait for Apple to provide a public cross-process view embedding API.
- **Remote/explicit window model** — treat the bridge-owned mac editor as the
  primary interaction surface and stop forcing arbitrary editors through a
  synthetic embedded-input contract.

The floating window approach works reliably and is the established pattern
used by jBridge, Blue Cat's PatchWork, and other professional bridges.

## Evidence Requirements

- Plugin editor visible inside REAPER's FX window (not floating)
- Mouse clicks on the editor work (knobs, buttons)
- Keyboard input works where applicable

Current posture: rendering evidence exists; universal interactive support does
not.
