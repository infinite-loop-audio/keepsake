# G01.013 — Embedded Editors

Status: ready
Owner: Infinite Loop Audio
Updated: 2026-04-11
Governing refs:
  - docs/architecture/system-architecture.md
Auto-continuation: allowed within g01

## Scope

Replace floating editor windows with true embedding into the host's plugin
window. Currently, Keepsake opens a separate floating window for each editor.
Most DAWs expect plugins to draw inside a host-provided parent window.

This is technically complex because the editor runs in a different process
than the host.

## Steps

### 1. Research cross-process embedding per platform

- **Windows**: `SetParent(editor_hwnd, host_hwnd)` — well-established for
  cross-process embedding. jBridge and Carla both use this. The bridge
  creates the editor, the host calls SetParent to reparent it.
- **Linux**: `XReparentWindow(display, editor_window, host_window, x, y)` —
  standard X11 cross-process reparenting. XEmbed protocol for proper
  focus and event handling.
- **macOS**: no direct cross-process NSView embedding. Options:
  - `CARemoteLayer` / `IOSurface` — off-screen render in bridge, composite
    in host. This is what Apple's AUv3 hosting uses internally.
  - `NSViewServiceMarshal` — private API, not suitable.
  - Maintain floating window on macOS as fallback until a reliable
    embedding mechanism is proven.

Document the chosen approach per platform before implementing.

### 2. CLAP GUI extension update

Change `is_api_supported` to report embedded (non-floating) support on
platforms where embedding works (Windows, Linux). macOS stays floating
unless CARemoteLayer proves viable.

Implement `set_parent()` — receive the host's window handle, send it to
the bridge via a new `EDITOR_SET_PARENT` opcode, bridge reparents the
editor window.

### 3. Platform implementations

- Windows: `SetParent()` in `bridge_gui_win.cpp`
- Linux: `XReparentWindow()` in `bridge_gui_linux.cpp`
- macOS: research CARemoteLayer, implement if viable, otherwise keep
  floating

### 4. Focus and input handling

Cross-process window embedding has focus/keyboard challenges:
- Windows: `SetFocus` across process boundaries, keyboard hooks
- Linux: XEmbed focus protocol
- Test with plugins that have text input fields

## Evidence Requirements

- Plugin editor embedded inside a CLAP host's plugin window on at least
  one platform (Windows or Linux)
- Keyboard and mouse input working in the embedded editor

## Stop Conditions

- Stop if macOS cross-process embedding proves unreliable — floating
  windows are an acceptable permanent fallback on macOS
- Stop if focus handling causes host instability
