# Known Issues — v0.1-alpha

Status: active
Owner: Infinite Loop Audio
Updated: 2026-04-12

This is the release-baseline caveat list for the first public alpha. It should
shrink or sharpen as g02 validation runs. Do not broaden support claims around
it; either validate the lane or keep the caveat.

## Support Posture

- Primary validated lane today: macOS + REAPER + VST2
- Experimental / lightly proven: Windows, Linux, VST3, AU v2, 32-bit

Experimental here means implementation exists and may work, but the alpha
should not imply equal confidence without fresh release-window evidence.

## Current Known Issues

### Host and cache behavior

- REAPER can rewrite its cached descriptor list for `keepsake.clap` based on a
  targeted one-plugin scan result. The guarded smoke tooling now preserves the
  user's REAPER cache, but manual rescans can still invalidate expectations if
  the scan scope is narrower than the installed bundle's intended descriptor
  set.
- Cold host discovery time can vary because descriptor discovery still includes
  bounded plugin scan work on first use.

### Format and validation coverage

- VST2 is the strongest proven format lane right now.
- VST3 and AU v2 loaders exist in tree, but broad alpha claims for them still
  need explicit release-window validation.
- 32-bit support is an architectural goal and code path, but it should remain
  experimental until release-window proof exists on supported platforms.

### Platform coverage

- macOS has the strongest real-host evidence.
- Windows and Linux have green CI builds, but CI success alone is not yet the
  same as full alpha support proof for real-host GUI and runtime behavior.

### GUI/editor behavior

- macOS plugin GUI handling is strongest in REAPER today.
- Cross-process embedded editor behavior remains platform- and host-sensitive.
  The release should not imply that every editor mode is equally settled across
  hosts.
- AU GUI handling is not yet part of the strongest proven alpha lane.

### Packaging and install surface

- No public binary release has shipped yet.
- Signed/notarized vs unsigned macOS alpha distribution is still a release
  decision, not a settled user promise.
- Build-from-source and install docs still need a release-grade pass before the
  first public alpha.

## Release Rule

If a release claim conflicts with this file, either:

1. validate the lane and update this file, or
2. narrow the claim

Do not publish around unresolved ambiguity.

## Next Task

Use this file as a hard gate during g02.004 publication prep: narrow release
claims if any line here cannot yet be backed by the validation matrix.
