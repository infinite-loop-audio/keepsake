# G01.017 — IOSurface Embedded Editors (macOS)

Status: ready
Owner: Infinite Loop Audio
Updated: 2026-04-11
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

## Evidence Requirements

- Plugin editor visible inside REAPER's FX window (not floating)
- Mouse clicks on the editor work (knobs, buttons)
- Keyboard input works where applicable
