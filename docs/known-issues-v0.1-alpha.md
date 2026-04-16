# Known Issues — v0.1-alpha

Status: active
Owner: Infinite Loop Audio
Updated: 2026-04-12

This is the release-baseline caveat list for the first public alpha. It should
shrink or sharpen as g02 validation runs. Do not broaden support claims around
it; either validate the lane or keep the caveat.

## Support Posture

- Primary validated lane today: macOS + REAPER + VST2
- Experimental but now real-host-proven: Windows, Linux
- Experimental / lightly proven: VST3, AU v2, 32-bit

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
- Windows and Linux now both have exploratory real-host VM evidence in REAPER,
  but neither has enough release-window proof yet to stand beside the primary
  macOS lane.
- Linux now has exploratory Ubuntu ARM64 VM host evidence with native ARM64
  REAPER and the repo `test-plugin.so`, including scan/add/UI/transport
  success. The remaining caveat is that this is still ARM64 VM evidence, not a
  direct `linux-x64` host-validation pass against the current public artifact
  target.
- Windows now has exploratory Windows 11 ARM64 VM host evidence with x64
  REAPER and the repo `test-plugin.dll`, including scan/add/UI success. The
  remaining caveats are that this is still ARM64 VM evidence, transport was not
  exercised yet, and the lane has not been proven with a non-repo legacy
  plugin.

### GUI/editor behavior

- macOS plugin GUI handling is strongest in REAPER today.
- On macOS, the supported bridged-editor posture is now the bridge-owned live
  editor window. This is the path validated in the strongest current lane for
  Serum, APC, and Khords.
- The old IOSurface embedded preview path still exists in tree, but it should
  be treated as diagnostic / experimental only. The release should not imply
  that interactive embedded editing is a settled or generally supported macOS
  mode.
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
