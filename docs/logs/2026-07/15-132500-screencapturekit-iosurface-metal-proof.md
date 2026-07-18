# 2026-07-15 13:25 — ScreenCaptureKit → IOSurface → Metal proof

## Summary

A native `arm64` process can present an `x86_64` VST2 editor from a Rosetta
helper cleanly and at display rate. The working path is:

```text
x86_64 AppKit plugin window
  → WindowServer / ScreenCaptureKit
  → IOSurface-backed CVPixelBuffer
  → native arm64 Metal drawable
```

This changes the IOSurface finding. The earlier slow and unstable result was
not an IOSurface transport limit. The weak part was producing and presenting
frames through manual AppKit capture and CPU-oriented copying.

## Implementation

Added `mac-vst2-sck-proof`, a native macOS research executable. It:

- launches the existing `x86_64` local VST2 host under Rosetta
- discovers its compositor window with ScreenCaptureKit
- rejects transient zero-sized window descriptors before starting capture
- requests 60 Hz BGRA frames
- keeps only the newest `CVPixelBuffer`
- renders the IOSurface-backed buffer into an `MTKView` with Core Image/Metal
- performs no CPU pixel readback or `IOSurfaceLock` in the frame path
- reports received, presented, and incomplete frame counts

The helper can create a borderless source window, so the native output contains
only the plugin editor rather than nested window chrome.

## Results

Repo demo fixture, borderless source:

```text
source points=322x160 pixels=644x320
final received=867 presented=867 incomplete=4 elapsed=15.29s avg-fps=56.7
```

The output screenshot was sharp, correctly oriented, complete, and showed the
animated parameter at its current value.

Real installed Intel APC VST2:

```text
source points=942x600 pixels=1884x1200
final received=1165 presented=1165 incomplete=0 elapsed=20.21s avg-fps=57.7
```

The native output screenshot contained the complete APC editor. No black frame,
partial repaint, tearing, or clipped child view was visible.

An early run exposed a real startup race: ScreenCaptureKit briefly advertised
the new child window with a zero-sized frame. That produced a 1×1 stream and no
Metal presentations. Capture now waits for valid geometry.

## What this proves

- ScreenCaptureKit can capture an Intel plugin window owned by another process.
- Its IOSurface-backed frames can feed a native Metal host window without CPU
  readback.
- The output path handles a real problematic editor, not only the repo fixture.
- IOSurface is capable of smooth window presentation when WindowServer produces
  the frames and the receiver stays on the GPU.

## What this does not prove

- universal mouse, keyboard, focus, IME, drag/drop, or accessibility routing
- resize and scale transitions across arbitrary plugins and displays
- a shipping Screen Recording permission/TCC posture
- recovery from helper crash, window replacement, or plugin modal windows
- suitability for every DAW host

The proof currently captures the Rosetta helper from the native parent.
ScreenCaptureKit permission identity must be designed before integration. A DAW
must not unexpectedly inherit a Screen Recording prompt from a hosted plugin.

## Decision

- Keep CARemote stopped; it still has no non-black presentation proof.
- Reopen IOSurface as a viable high-performance output transport.
- Treat ScreenCaptureKit + Metal as the preferred streaming-output prototype.
- Keep the existing bridge-owned live window as the supported interactive path
  until input and TCC contracts are proven.
- Use the disposable `x86_64` local host as the source-window owner.

## Validation

- `effigy demo:build:mac-vst2-sck-proof`
- automated 15-second repo fixture run
- automated 20-second APC VST2 run
- manual output-window screenshots for both runs
- architecture check with `file`: native proof is `arm64`; local host and demo
  plugin are `x86_64`

## Next Task

Turn the proof into a signed helper prototype with stable TCC identity, explicit
content geometry, resize/window-replacement handling, and an input protocol.
Validate clicks, drags, keyboard focus, and screenshot capture against APC and
Khords before changing the supported DAW posture.

## Continuation — input and app identity

The native output was packaged as `KeepsakeVst2UiProof.app` with bundle id
`audio.infiniteloop.keepsake.vst2-ui-proof`, the required ScreenCaptureKit usage
description, and an ad-hoc development signature.

Added a versioned JSON-lines input pipe. The native `MTKView` forwards normalized
pointer, scroll, and key commands. The Rosetta helper resolves the real plugin
view and delivers AppKit events locally.

Two defects found during validation materially affected reliability:

- compositor geometry can be nonzero but still mid-animation; capture now
  requires three stable geometry samples
- the helper had not activated the VST2 before opening its editor; adding sample
  rate, block size, and `effMainsChanged(1)` enabled APC interaction receipts

Real input through the signed app bundle changed the demo gain from 60% to 17%
with a complete pointer automation receipt. A real `a` key changed it to 75%
with a keyboard automation receipt. APC and Khords both produced complete native
output; APC input reached its JUCE view and an activated fader run produced a
VST2 automation transaction.

Global Quartz event posting was rejected. It failed without event-posting trust
and would add an Accessibility permission boundary. Same-process AppKit target
delivery with preserved JUCE gesture timing is the prototype contract.

## Continuation — structured lifecycle receipts

Replaced controller log scraping with a dedicated JSONL receipt channel. The
Rosetta helper duplicates its original stdout for receipts, then redirects
ordinary helper and plugin stdout to stderr before loading the VST2. The native
controller validates and re-emits helper receipts alongside its own lifecycle
receipts.

Startup now keys ScreenCaptureKit discovery from the helper's exact `pid` and
`CGWindowID`; title matching is gone. Receipts cover controller launch, helper
readiness, three-sample window stability, stream configuration/readiness, first
IOSurface frame, pointer/key automation, final telemetry, graceful stopping,
and helper exit. Load, editor, window, frame, and stream failures produce
structured errors.

Validation results:

- repo fixture: 12 valid JSON receipts, exact 320×160 helper geometry, first
  640×320 frame, pointer automation `0/0/0 → 2/2/2`, key automation
  `2/2/2 → 3/3/3`, and graceful helper exit `0`
- invalid plugin path: structured `plugin-load` error, controller exit `4`, and
  preserved helper exit `3`
- APC: ten valid JSON receipts, exact 960×580 helper geometry, first
  1920×1160 frame, 143 received / 140 presented frames at 43.0 average fps,
  graceful helper exit `0`, and no non-JSON stdout lines

## Continuation — live resize and screenshot commands

Added a controller stdin command channel with request-scoped PNG screenshot
receipts. Screenshot encoding retains the latest complete frame and performs
CPU readback only for that explicit request. Accepted writes finish before
shutdown closes stdout.

Added strict source monitoring keyed to the helper-authorized `pid` and
`CGWindowID`. The fixture's `R` key now issues a real VST2
`audioMasterSizeWindow` callback. The helper resized its compositor window from
320×160 to 480×240; after three stable WindowServer bounds samples, the
controller updated the running stream from 640×320 to 960×480 without restart.
The final `resized` receipt is gated on receipt of a complete 960×480 frame, so
an immediate screenshot request cannot race and capture the old dimensions.

Repeated `SCShareableContent` descriptors did not surface the changed geometry
reliably in this run. WindowServer bounds from `CGWindowListCopyWindowInfo` did.
This matches Apple's API split: Core Graphics exposes exact window bounds, while
ScreenCaptureKit stream output dimensions remain fixed until the client calls
`updateConfiguration`. See [CGWindowListCopyWindowInfo](https://developer.apple.com/documentation/coregraphics/cgwindowlistcopywindowinfo%28_%3A_%3A%29),
[SCStream.updateConfiguration](https://developer.apple.com/documentation/screencapturekit/scstream/updateconfiguration%28_%3Acompletionhandler%3A%29),
and [WWDC22: Take ScreenCaptureKit to the next level](https://developer.apple.com/videos/play/wwdc2022/10155/).

Validation results:

- resized fixture PNG: 960×480, visually complete, no stale 640×320 frame
- APC PNG: 1920×1160, visually complete, no false source-loss receipt
- screenshot-before-readiness: request-scoped `screenshot` error
- stdin shutdown: graceful helper exit `0`

## Continuation — helper-authorized hot source replacement

Added an explicit source-replacement lifecycle. The helper can move the existing
plugin editor parent into a new AppKit window, retarget its local input receiver,
and publish a higher window revision plus the new exact `CGWindowID`. The
controller never infers a replacement from process ownership or window order.

After the new identity passes three stable ScreenCaptureKit snapshots, the
controller calls `updateContentFilter` on the existing `SCStream`, clears its
retained old frame, and reapplies the pixel configuration. The final
`replaced/stream` receipt is gated on a matching frame. Monitoring restarts only
for the new exact identity. An unannounced disappearance still fails as
`source-window-lost`.

Validation results:

- same-size fixture window ID changed from `42694` to `42696`, revision `1` to
  `2`
- running stream kept 320×160 points / 640×320 pixels while switching filters
  without restart; a serial capture-queue barrier excluded queued old frames
- first accepted post-replacement PNG: 640×320, visually complete and showing
  the post-swap parameter change
- scripted post-replacement key reached `KeepsakeDemoEditorView` and produced
  begin/automate/end `0/0/0 → 1/1/1`
- helper shutdown remained graceful with exit `0`
- APC regression: 1920×1160 first frame, 180 received / 178 presented,
  zero unavailable frames, helper exit `0`
