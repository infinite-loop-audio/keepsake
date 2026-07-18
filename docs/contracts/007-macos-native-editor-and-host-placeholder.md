# 007 — macOS Native Editor and Host Placeholder

Status: active
Owner: Infinite Loop Audio
Updated: 2026-07-16
Architecture refs:
  - docs/architecture/macos-bridged-ui-options.md
  - docs/architecture/system-architecture.md
Evidence refs:
  - docs/logs/2026-04/16-235900-macos-live-editor-posture-closeout.md
  - docs/logs/2026-07/15-162700-soundcheck-companion-inspection-integration.md

## Scope

This contract governs the normal macOS editor path for Keepsake CLAP
instances. It applies to every CLAP host. No companion or host-specific API is
part of this boundary.

## Host Contract

- Keepsake advertises a non-floating Cocoa CLAP GUI as its preferred surface.
- When the host supplies a parent `NSView`, Keepsake attaches one non-rendering
  placeholder view to it.
- The placeholder identifies the selected legacy plugin and states that the
  interactive editor is in a separate native window.
- The placeholder provides one lifecycle action: reopen the native editor
  after its window has been closed.
- The bridge posts a per-instance Darwin notification when shared editor state
  changes. The placeholder receives it on the main dispatch queue and then
  reads shared memory as the source of truth; no timer or host audio callback
  is required.
- The placeholder does not mirror pixels, forward plugin input, or imitate the
  legacy editor.
- Hosts that explicitly request a floating Cocoa GUI remain supported and may
  omit the placeholder.

## Native Editor Contract

- `gui.show()` opens the editor in the existing bridge process.
- The bridge owns the top-level native window and the real plugin `NSView`.
- The native editor uses AppKit's modal-panel window level and Keepsake restores
  that level after activation changes. This keeps it above host plugin panels,
  which commonly use the floating level, while leaving menus and system UI
  above it.
- AppKit events reach that window normally. Keepsake does not synthesize or
  forward pointer and keyboard events across processes.
- Closing or hiding the CLAP GUI closes the native editor through the normal
  editor IPC lifecycle.
- Audio, parameters, state, and isolation remain on the ordinary Keepsake CLAP
  and bridge paths.

## Screenshot Contract

Keepsake exposes no private screenshot API.

Inspection hosts should capture the native top-level editor window through
their general window-capture path, exactly as they would for any other plugin
that opens a floating or auxiliary editor. A host may identify the window from
the newly visible window set and Keepsake's stable window title. ScreenCaptureKit
is suitable for this host-owned capture, but its frames must not be streamed
back into the CLAP parent view.

Screen Recording permission, window discovery, crop/chrome policy, and output
encoding belong to the host. They are not part of Keepsake's plugin ABI.

## Explicit Non-Goals

- no Soundcheck-specific executable, dylib, helper protocol, or descriptor API
- no remote `NSView` attachment
- no CARemoteLayer product path
- no ScreenCaptureKit presentation inside the host plugin window
- no synthetic AppKit input
- no second Keepsake-owned imitation of the host window

## Acceptance

- a normal CLAP host receives a valid Cocoa child view
- the host view remains non-rendering and stable
- closing the native editor leaves the host view open and enables its reopen
  action
- the real legacy editor appears in its bridge-owned native window
- the native editor remains above normal host and host-plugin windows
- the native editor receives ordinary mouse and keyboard input
- removing all companion inspection targets does not affect scanning, audio,
  parameters, state, or the native editor path

## Next Task

Validate the placeholder plus native-window lifecycle in Soundcheck and REAPER,
then implement native-window screenshots only in Soundcheck's generic floating
window capture lane.
