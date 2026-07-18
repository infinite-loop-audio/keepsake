# 2026-07-16 11:42 — macOS native editor placeholder refocus

Status: implemented; manual host validation pending

## Drift

The companion experiment escaped its research boundary. It added a dedicated
app, receiver dylib, Intel helper, ScreenCaptureKit accumulator, input
forwarding, focus emulation, and Soundcheck-specific lifecycle contract.

It still failed the product bar:

- APC input did not work reliably
- several plugin UIs still shimmered or glitched
- Soundcheck no longer exercised Keepsake like an ordinary CLAP host
- screenshot support became coupled to an alternate hosting topology

## Decision

Return to one macOS product path:

- host requests a normal non-floating Cocoa CLAP GUI
- Keepsake attaches a non-rendering placeholder to the supplied parent
- the bridge opens the real legacy editor in its own native window
- all plugin interaction stays native
- screenshot capture belongs to a host's general floating-window capture path

The failed companion code and build targets are removed. Its prior logs remain
as stop-condition evidence.

Soundcheck's `KeepsakeVst2` inspection processor, inspection dylib loader, and
streamed screenshot override are also removed. Keepsake-backed VST2 entries
now load through Soundcheck's ordinary in-process CLAP processor.

A second Soundcheck-specific override was found in its generic inspection
helper launcher: it set `KEEPSAKE_MAC_ENABLE_PREVIEW=1` and forced
`KEEPSAKE_MAC_UI_MODE=iosurface` for every helper. That override is removed and
covered by a focused Soundcheck regression test. Inspection helpers now inherit
the same plugin environment as ordinary host processes.

AIR Jura Chorus then exposed a native hosted-parent layout defect: its direct
JUCE editor view used a `y=91` frame origin inside a `630x549` container. The
bridge now normalizes direct hosted editor views to origin `(0,0)` after open
and after later frame changes, while leaving x64 parentless windows untouched.

Bridge-owned editor windows now use AppKit's modal-panel window level on both
the hosted-parent and parentless paths. Host plugin panels commonly use the
floating level, including Soundcheck's always-on-top inspection window; using
the same level left cross-process ordering unstable. The bridge restores the
modal-panel level during idle and app activation transitions, keeping the real
editor above host panels while leaving menus and system UI above it.

Closing the native editor no longer reports the host-owned CLAP placeholder as
closed. Its button changes from `Editor Open` to `Open Native Editor` when the
bridge reports the auxiliary window closed, then reuses the ordinary editor
open IPC lifecycle.

The initial button state depended on the host driving Keepsake's audio/main-
thread callback loop, so Soundcheck could leave `Editor Open` disabled after
the window closed. A short-lived AppKit timer proved the state boundary, then
was replaced with a per-instance Darwin notification. The bridge posts on
editor-state transitions; the placeholder receives the event on the main
dispatch queue and reads shared memory as the source of truth. The old floating
state poll is removed.

## Contract

Contract 007 now governs the non-rendering host placeholder and bridge-owned
native editor. The placeholder has one lifecycle control to reopen a closed
native editor. It explicitly excludes Soundcheck APIs, remote presentation,
synthetic plugin input, and a Keepsake screenshot ABI.

## Next Task

Fully restart Soundcheck and validate APC interaction. Then add native
top-level window capture to Soundcheck as generic host behavior, usable by any
plugin that owns an auxiliary editor window.
