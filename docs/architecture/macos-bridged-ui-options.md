# macOS Bridged UI Options

Status: draft
Owner: Infinite Loop Audio
Updated: 2026-07-16
Vision refs: docs/vision/001-keepsake-vision.md
Related roadmap refs:
  - docs/roadmaps/g01/013-embedded-editors.md
  - docs/roadmaps/g01/017-iosurface-embedded-editors.md
Related evidence:
  - docs/logs/2026-04/16-221500-mac-iosurface-embedded-ui-architecture-decision.md
  - docs/logs/2026-07/15-123000-macos-x64-editor-hosting-research.md
  - docs/logs/2026-07/15-132500-screencapturekit-iosurface-metal-proof.md

## Problem

Keepsake now has a useful macOS IOSurface rendering baseline, but the current
cross-process embedded-input model has failed to become a dependable universal
interaction contract for bridged editors.

What is proven:

- cross-process editor rendering can be surfaced into the host window
- crop rect and geometry alignment can be made correct
- some editors, especially Serum-class VSTGUI editors, can interact partially
- JUCE-based editors such as APC and Khords still fail the universal embedded
  interaction bar even after substantial input-delivery work

What was exhausted in the current lane:

- attach-target and parent-healing variants
- responder-chain selection
- `NSPanel` versus `NSWindow`
- offscreen versus visible bridge host windows
- `NSEvent`-based mouse synthesis
- `CGEvent`-based mouse synthesis

The current result is not strong enough to keep treating
"cross-process embedded bitmap plus injected AppKit input" as the default
universal macOS bridged UI architecture.

## Constraints

- Keepsake remains CLAP as the outer plugin format.
- Audio isolation remains a first-class requirement.
- macOS must support Apple Silicon hosts loading Intel plugins via a helper.
- The legal posture does not change: VeSTige only for VST2.
- The chosen UI model must distinguish:
  - what is validated and shippable
  - what is experimental

## Candidate Models

### Option A — Render-only embed plus interactive remote window fallback

Use embedded IOSurface presentation only as a visual/inspection surface. When a
plugin class fails the interaction contract, the bridge owns the true
interactive editor window and the host exposes an explicit "open live editor"
path.

#### Advantages

- Preserves the rendering work already completed.
- Gives users a usable path for problematic editors without blocking the whole
  macOS release lane.
- Keeps the architecture explicit instead of pretending embedded interaction is
  universal when it is not.

#### Costs

- Two editor modes on macOS increase user-facing complexity.
- Host UX must make the fallback explicit and predictable.
- Not a true "embedded works everywhere" answer.

#### Best fit

- Short-to-medium term alpha posture
- Honest support claims
- Fastest path to dependable behavior without discarding current work

### Option B — Remote/bridge-owned interactive window model

Treat the bridge-owned editor window as the primary macOS interaction surface.
The host plugin exposes controls, state, and transport behavior, but plugin UI
interaction stays with the bridge window instead of synthetic embedded input.

#### Advantages

- Aligns with the strongest proven interaction path on macOS.
- Avoids pretending AppKit event injection is a durable universal solution.
- Matches the direction used by many practical remote/plugin-server systems.

#### Costs

- Gives up true embed as the mainline interaction path.
- Requires stronger focus/window-ownership UX work.
- Host-side automation and parameter presentation need to stay good enough that
  the split UI model remains understandable.

#### Best fit

- Stable general-purpose macOS support
- Broader plugin compatibility over visual integration purity

### Option C — Stronger embedded host/bridge presentation contract

Replace the current synthetic AppKit input model with a fundamentally stronger
cross-process contract. Examples include a different event transport model,
framework-specific hooks, or a host/bridge coordination layer beyond generic
mouse/key injection.

#### Advantages

- Keeps the possibility of true embedded interaction alive.
- Could eventually produce a cleaner user experience than remote windows.

#### Costs

- Highest complexity and research risk.
- Current evidence says this can consume large amounts of time without proving
  universality.
- Very likely to become framework-specific in practice, which weakens the
  original "universal" goal.

#### Best fit

- A dedicated research lane after a stable fallback exists
- Not the first choice for the alpha support posture

### Option D — Format/framework-scoped support claims

Narrow macOS support claims instead of claiming universal bridged UI
interaction. For example: certain editor classes are supported in embedded
mode, while others are explicitly fallback-only or unsupported.

#### Advantages

- Honest and low-risk.
- Can coexist with any of the other options.

#### Costs

- Support matrix becomes more complicated.
- Users may view this as inconsistent unless the rule is extremely clear.

#### Best fit

- Release messaging and alpha support envelope
- Support policy, not a full architecture by itself

## 2026-07 abandoned companion experiment

CARemoteLayer transport could be established but rendered black. A later
ScreenCaptureKit experiment rendered the Intel editor into Soundcheck's host
view, but needed a dedicated helper, receiver dylib, frame accumulator, input
protocol, focus emulation, and Soundcheck-specific lifecycle handling. JUCE
input remained unreliable and other editors still shimmered.

That experiment is closed. Its dated logs remain evidence, but its executable,
library, protocol, and product integration surfaces are removed. ScreenCaptureKit
remains useful only for a host's generic screenshot of the real native window.

## Recommendation

Adopt **Option B as the product direction**. Pair it with a passive host-owned
placeholder so hosts still receive a normal Cocoa CLAP child view.

Reasoning:

- Option C is still interesting, but it is no longer the right default
  execution assumption.
- The live bridge-owned editor path is now the strongest proven interaction
  lane in real REAPER validation across Serum, APC, and Khords.
- Option A still preserves the useful IOSurface rendering baseline, but it no
  longer deserves equal product posture with the live editor path.
- Option B is now the default macOS UI stance, not just a fallback candidate.
- Option D should be used regardless, so public claims stay aligned to actual
  behavior.
- The CARemote form of Option C reached its stop condition: transport worked,
  presentation stayed black.
- The ScreenCaptureKit companion path exceeded its complexity budget without
  delivering universal interaction or stable rendering.
- A host can screenshot the real native window without changing Keepsake's
  CLAP behavior or streaming that window back into the host parent.

## Proposed Prototype Sequence

1. Advertise a non-floating Cocoa CLAP GUI and attach a non-rendering placeholder to
   the host-provided parent.
2. Open the real editor in the existing bridge-owned native window.
3. Validate that path with the current comparison set:
   - Serum
   - APC
   - Khords
4. Remove the companion receiver/helper/proof build surfaces.
5. Implement screenshots as general native-window capture in inspection hosts,
   outside Keepsake's ABI.

## Open Questions

- Does the bridge-owned editor need additional transport/focus coordination to
  feel first-class in hosts like REAPER?
- Should the diagnostic IOSurface preview be removed completely in a later
  cleanup, or retained behind its existing explicit operator gate?

## Decision Gate

Do not resume remote presentation or synthetic input without a new architecture
decision and a substantially different mechanism. The product path is native
editor plus a non-rendering host placeholder with one native-editor reopen action.

## Next Task

Validate contract 007 in ordinary Soundcheck and REAPER hosting, then move
screenshot work to Soundcheck's general floating-window capture path.
