# G01.006 — GUI Forwarding

Date: 2026-04-10
Milestone: g01.006
Status: complete

## What was done

### IPC protocol (editor opcodes)

- `EDITOR_OPEN` (0x10) — bridge opens VST2 editor in a floating window
- `EDITOR_CLOSE` (0x11) — bridge closes editor
- `EDITOR_GET_RECT` (0x12) — bridge returns editor dimensions

### Bridge GUI (macOS — Cocoa)

- `src/bridge_gui.h` — cross-platform GUI interface
- `src/bridge_gui_mac.mm` — macOS implementation using NSApplication +
  NSWindow + effEditOpen/effEditClose/effEditIdle
- `src/bridge_gui_stub.cpp` — stub for Windows/Linux (returns false)
- Bridge main loop uses poll timeout (16ms / ~60fps) when editor is open
  to interleave pipe messages with Cocoa event processing
- Editor window is floating (NSFloatingWindowLevel), auto-closes on user
  dismiss, cleans up on bridge shutdown

### CLAP GUI extension

- Full `clap_plugin_gui_t` implementation in plugin.cpp
- Reports floating window support only (not embedded)
- Platform-correct API: `CLAP_WINDOW_API_COCOA` on macOS
- Editor size queried from bridge at init time
- `show()` sends EDITOR_OPEN, `hide()` sends EDITOR_CLOSE
- Registered in get_extension when plugin has editor (`effFlagsHasEditor`)

## Files created

- `src/bridge_gui.h` — GUI abstraction
- `src/bridge_gui_mac.mm` — macOS Cocoa implementation
- `src/bridge_gui_stub.cpp` — Windows/Linux stub

## Files modified

- `src/ipc.h` — editor opcodes, IpcEditorRect struct
- `src/bridge_main.cpp` — editor handlers, GUI-aware main loop
- `src/plugin.h` — editor state fields
- `src/plugin.cpp` — CLAP GUI extension, editor open/close/size
- `CMakeLists.txt` — bridge_gui source selection, Cocoa framework linking

## Known limits

- **Floating window only** — not embedded in host window (would require
  CARemoteLayer/IOSurface for cross-process embedding on macOS)
- **macOS only** — Windows (Win32) and Linux (X11/XEmbed) GUI stubs need
  implementation
- **No DPI scaling** — VST2 editors are fixed-size
- **Window positioning** — floating window appears at a default position,
  not anchored to host's plugin slot

## Next Task

Ship g01.007 (CI) or g01.008 (VST3/AU loaders).
