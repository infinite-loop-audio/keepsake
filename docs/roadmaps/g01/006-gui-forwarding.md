# G01.006 — GUI Forwarding

Status: complete
Owner: Infinite Loop Audio
Updated: 2026-04-10
Governing refs:
  - docs/contracts/004-ipc-bridge-protocol.md
  - docs/architecture/system-architecture.md
Auto-continuation: allowed within g01

## Scope

Forward VST2 plugin editor GUIs from the bridge subprocess to the CLAP host
window, so users can interact with plugin interfaces. This is the biggest
user-facing gap — without it, bridged plugins are headless.

This is technically complex because the editor runs in a different process
and needs a window handle from the host's process.

## Steps

### 1. Research platform embedding approaches

Study how existing bridges handle cross-process GUI:

- **macOS**: the bridge can create an NSView and the host provides a parent
  NSView handle. Cross-process window embedding on macOS uses
  `NSView.addSubview` or `CALayer` reparenting. Carla's approach and
  JUCE's inter-process editor model are references.
- **Windows**: `SetParent()` for cross-process HWND embedding is
  well-established (used by jBridge and others).
- **Linux**: X11 `XReparentWindow()` for cross-process embedding is the
  standard approach (used by Carla, Ardour, etc.).

Document the chosen approach before implementation.

### 2. Extend IPC protocol for GUI

Add GUI-related opcodes to the bridge protocol:

- `EDITOR_OPEN` — host sends parent window handle, bridge opens VST2 editor
  with `effEditOpen(parent_handle)`
- `EDITOR_CLOSE` — bridge closes editor with `effEditClose`
- `EDITOR_GET_RECT` — bridge queries editor size via `effEditGetRect`
- `EDITOR_IDLE` — host sends periodic idle ticks, bridge calls `effEditIdle`
- `EDITOR_RESIZE` — bridge notifies host of size change

### 3. CLAP GUI extension

Implement `clap_plugin_gui_t`:

- `is_api_supported` — report support for the platform's native API
  (cocoa on macOS, win32 on Windows, x11 on Linux)
- `create` / `destroy` — lifecycle
- `set_parent` — receive host's window handle, forward to bridge
- `get_size` / `set_size` — query/set editor dimensions
- `show` / `hide`

### 4. Platform-specific editor hosting

Implement the bridge-side editor hosting for each platform:

- macOS: `effEditOpen` with `NSView*`, run `effEditIdle` on a timer
- Windows: `effEditOpen` with `HWND`, message pump for editor
- Linux: `effEditOpen` with X11 `Window`, XEmbed protocol

### 5. Evidence

- Open a VST2 plugin editor through the bridge in a CLAP host
- Interact with the editor (change parameters, verify audio changes)
- Close and reopen the editor

## Stop Conditions

- Stop if cross-process window embedding is not reliable on a given platform
  (document the limitation, ship without GUI on that platform)
- Stop if editor idle timing causes audio glitches (investigate threading
  model)

## Evidence Requirements

- VST2 plugin editor visible and interactive in a CLAP host
- Parameter changes in the editor reflected in the host
- Works on at least macOS

## Next Task

After this milestone: CI (007) or VST3/AU loaders (008).
