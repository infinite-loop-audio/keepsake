# G01.012 — Windows and Linux GUI Backends

Status: planned
Owner: Infinite Loop Audio
Updated: 2026-04-11
Governing refs:
  - docs/architecture/system-architecture.md
Auto-continuation: allowed within g01

## Scope

Implement the GUI backends for Windows (Win32) and Linux (X11) so that
plugin editors work on all three platforms. The macOS Cocoa backend is
already working (g01.006). This milestone fills in the stubs.

## Steps

### 1. Windows GUI backend (`bridge_gui_win.cpp`)

- Create a Win32 window (`CreateWindowEx`) for the editor
- Call `effEditOpen` with the HWND
- Message pump (`GetMessage`/`DispatchMessage`) interleaved with pipe I/O
- `effEditIdle` on a timer
- Window management: sizing from `effEditGetRect`, close handling
- Header bar: child HWND toolbar at the top of the editor window

For embedded mode (future): `SetParent()` to reparent the editor window
into the host's HWND. This is well-established on Windows (jBridge,
Carla, Blue Cat's PatchWork all do it).

### 2. Linux GUI backend (`bridge_gui_linux.cpp`)

- Create an X11 window for the editor
- Call `effEditOpen` with the X11 Window ID
- X11 event loop interleaved with pipe I/O via `select()` on both the
  X11 connection fd and the pipe fd
- `effEditIdle` on a timer
- XEmbed protocol support for embedded mode (future)

### 3. CMake integration

- Platform-select the GUI source file (already done for macOS/stub)
- Add Win32 libraries (`user32`, `gdi32`) on Windows
- Add X11 libraries (`X11`, optionally `Xembed`) on Linux

## Evidence Requirements

- Plugin editor visible on Windows
- Plugin editor visible on Linux
- Header bar displaying on both platforms

## Stop Conditions

- Stop if X11 editor embedding requires toolkit dependencies (GTK, Qt)
  that would bloat the bridge binary — use raw X11 instead
