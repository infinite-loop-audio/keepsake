# 2026-07-15 16:27 — Soundcheck companion inspection integration

Status: superseded 2026-07-16

## Correction

This batch proved the standalone ScreenCaptureKit controller launch and
screenshot path, but did not integrate the stream into Soundcheck's inspection
window. Launching a second output window bypassed Soundcheck's real host view
and controls. The installed proof is therefore diagnostic evidence, not a
completed Soundcheck integration.

## Final disposition

The in-process receiver, Rosetta inspection helper, forwarding protocol, and
proof app were removed. APC input remained unreliable and broader plugin UI
rendering still shimmered. The experiment also made Soundcheck behave unlike a
normal CLAP host.

The replacement contract is host-independent: Soundcheck and DAWs receive a
passive Keepsake placeholder, while the real plugin stays interactive in the
bridge-owned native window. Screenshots target that native window through the
host's general window-capture path.

## Implementation

- made `KeepsakeVst2UiProof.app` self-contained with its signed Rosetta helper
- moved descriptor-ID construction into shared Keepsake code
- added controller-side `--plugin-id` resolution against Keepsake's scan cache
- added clean output-window close shutdown
- passed Soundcheck's screenshot path through its private helper contract
- gated Soundcheck readiness on the controller's first frame and completed PNG
- kept the Soundcheck helper as parent-lifetime owner of the controller

## Installed Diagnostic Proof

The installed Soundcheck experiment resolved APC from
`keepsake.vst2.41706364`, launched the separate controller, opened the real
Intel VST2 under Rosetta, and wrote a clean 1920×1160 PNG. Closing the parent
pipe produced orderly controller and helper shutdown. It also produced two UI
surfaces and left Soundcheck's real inspection window out of the flow, so it
failed the product requirement.

Validation:

- `mac-vst2-sck-proof-app` build and strict deep-signature verification
- direct descriptor-ID APC launch and command-driven screenshot
- development Soundcheck helper → APC screenshot and parent-pipe shutdown
- installed Soundcheck helper → installed controller → APC screenshot
- Soundcheck `effigy test`: 13 Vitest and 55 Rust tests passed
- both repositories' Effigy docs QA passed

Keepsake exposes no Effigy test selector and its generated CTest project has no
registered tests. The real-plugin command/receipt proof is the executable
validation for this batch.

## Superseded Next Task

Replace the external controller with an in-process receiver attached to
Soundcheck's existing plugin-host `NSView`, then prove APC and the native
Capture action.

## Completed Integration

The external controller product path was removed. Soundcheck now creates its
normal inspection `NSWindow`, native header, and plugin-host `NSView`, then loads
Keepsake's receiver in that process. Keepsake creates only the covered Rosetta
source window and the child presentation/input view.

The failures during live APC proof had separate causes:

- SCK incremental frames expose pixel dirty rectangles. Replacing the retained
  frame with each IOSurface produced black UI with only APC's animated region.
- A Metal drawable was transient under repeated window capture.
- Core Animation's default `contents` transition restarted on every update and
  produced flashing/juddering.
- The covered source had to follow Soundcheck when its inspection window moved.

The implemented path merges dirty rectangles into a full BGRA accumulator,
copies it into pooled IOSurface-backed pixel buffers, and feeds
display-immediate samples to `AVSampleBufferDisplayLayer.sampleBufferRenderer`.
Input still travels through a normal child `NSView`; explicit screenshots still
encode the accumulator directly.

Installed validation used the current schema-v24 Soundcheck build:

- repeated real-window APC captures remained complete
- Soundcheck's Capture action wrote a clean 1920×1160 PNG
- pointer down/up reached APC's JUCE view
- APC/JUCE's mouse handler attempted to activate the helper application; the
  companion helper now uses a non-activating panel, ignores physical mouse
  input, and blocks plugin-originated AppKit activation after editor startup
- direct and integrated pointer/drag probes kept helper activation false while
  Soundcheck remained the user-facing focus owner
- the source window followed Soundcheck and remained covered
- ScreenCaptureKit permission survived the rebuilt signed app
- the Soundcheck inspection helper fell from about 57% CPU with per-frame
  `CGImage` materialization to 1.6–1.8% with sample-buffer presentation
- the Rosetta APC helper remained around 7–8% CPU

## Next Task

Expand the installed path across a JUCE/non-JUCE plugin matrix and stress
pooled-buffer lifetime, resize, and shutdown under animated editors.

## Continuation — forwarded activation and display rate

Blocking helper activation stopped APC from stealing focus, but legacy JUCE
editors also inspect application activity during mouse dispatch. The helper now
reports active only inside a forwarded pointer gesture while its real AppKit
activation remains false. A forwarded APC button click changed editor state
with `helper-active=0->0`.

The 30 Hz embedded stream made 60 Hz JUCE animation judder. Dirty-rectangle
telemetry confirmed that ScreenCaptureKit already reports Retina pixel
coordinates: APC produced rectangles as far right as `x=1804` in its 1920-pixel
frame. No coordinate scaling was required.

The receiver now requests 60 Hz, skips unchanged null dirty rectangles, merges
changes into its persistent IOSurface with Metal blits, and performs the full
pooled presentation copy on the GPU. A direct PNG from that accumulator was a
complete 1920×1160 APC editor. Soundcheck presentation CPU settled around 5%
during APC's continuously animated editor, down from about 10% for the 60 Hz
CPU-copy version.

## Next Task

Retest APC interaction and animated JUCE editors in installed Soundcheck, then
record any plugin-specific failures separately from the shared transport path.
