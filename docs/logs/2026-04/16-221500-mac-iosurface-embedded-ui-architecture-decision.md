# Decision Log: macOS Bridged Embedded UI Architecture Cutoff

Status: accepted
Created: 2026-04-16
Roadmap: g01.017

## Context

Keepsake's macOS IOSurface embedded-editor lane now has a useful rendering
baseline but has failed to produce a reliable universal interaction model for
bridged editors. The work in this thread covered:

- correct IOSurface sizing and crop rect detection
- host-side geometry alignment and resize handshakes
- bridge-side crop/input coordinate translation
- timer/cadence experiments for redraw stability
- responder-chain targeting
- `NSPanel` versus `NSWindow`
- offscreen versus visible bridge host windows
- synthetic `NSEvent` mouse delivery
- `CGEvent`-based button delivery

The strongest stable outcome is:

- Serum-class editors can render and interact partially in embedded mode
- note-active animated Serum patches still flicker
- JUCE-based editors such as APC and Khords render after geometry fixes but
  remain non-interactive in embedded mode

Tracing showed that click events do reach the JUCE view, but no
`BeginEdit/Automate/EndEdit` activity follows those clicks.

## Decision

Treat the current macOS cross-process embedded-input model as architecturally
non-viable for universal support.

Keepsake should preserve the current IOSurface rendering/cropping work as a
useful experimental baseline, but execution should stop assuming that
"cross-process embedded bitmap plus injected native input" can become the
default universal macOS editor strategy with incremental AppKit fixes.

Future macOS work should move to replacement architecture options rather than
continuing small event-injection tweaks.

## Consequences

- The current embedded lane is evidence, not a shippable universal interaction
  contract.
- macOS embedded rendering remains useful for diagnostics and focused
  prototypes, but should not be promoted as dependable broad support.
- Floating/remote-window presentation remains the only proven universal
  interaction fallback in the current architecture.
- The next design batch must compare replacement models rather than tweaking
  `NSEvent` or `CGEvent` synthesis again.
- Candidate replacement directions now include:
  - render-only embed plus explicit interactive remote window fallback
  - remote-window/server-style UI model on macOS
  - a stronger host/bridge presentation contract than synthetic AppKit input
  - narrowed support claims instead of universal embedded interaction claims

## Evidence

- [g01.017 roadmap note](../../roadmaps/g01/017-iosurface-embedded-editors.md)
- [14-091500-windows-apc-embed-thread-model.md](14-091500-windows-apc-embed-thread-model.md)
- harness lane in [tools/mac-clap-host.mm](/Users/tom/Dev/projects/keepsake/tools/mac-clap-host.mm)
- mac embedded host path in [src/plugin_gui_mac_embed.mm](/Users/tom/Dev/projects/keepsake/src/plugin_gui_mac_embed.mm)
- mac IOSurface bridge path in [src/bridge_gui_mac_iosurface.mm](/Users/tom/Dev/projects/keepsake/src/bridge_gui_mac_iosurface.mm)

## Next Task

Write a focused macOS UI architecture brief that compares replacement
presentation models and selects one prototype lane, instead of continuing
incremental embedded-input experiments.
