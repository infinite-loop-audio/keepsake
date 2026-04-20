# Known Issues — v0.1-alpha

Status: active
Owner: Infinite Loop Audio
Updated: 2026-04-20

This is the release-baseline caveat list for the first public alpha. It should
shrink or sharpen as post-alpha validation and claim-correction work continues.
Do not broaden support claims around it; either validate the lane or keep the
caveat.

## Support Posture

- Primary validated lane today: macOS + REAPER + VST2
- Experimental with current CI and real-host VM evidence: Windows
- Experimental with current CI and exploratory host evidence: Linux
- Experimental / lightly proven: VST3, AU v2
- Experimental public claim despite stronger Windows proof: 32-bit

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
- 32-bit support should still remain experimental in public claims for
  `v0.1-alpha`, but Windows now has real-host REAPER VM proof for APC/Serum
  `x86` rather than only architecture-level implementation.

### Platform coverage

- macOS has the strongest real-host evidence.
- The packaged macOS candidate artifact now has a fresh REAPER smoke pass for
  APC, Serum, and Khords, which is the current release-window baseline for the
  supported lane.
- Windows and Linux do not have equal proof quality:
  - Windows now has materially stronger REAPER VM evidence than the original
    release freeze captured
  - Linux remains exploratory host evidence
- Linux now has exploratory Ubuntu ARM64 VM host evidence with native ARM64
  REAPER and the repo `test-plugin.so`, including scan/add/UI/transport
  success. The remaining caveat is that this is still ARM64 VM evidence, not a
  direct `linux-x64` host-validation pass against the current public artifact
  target.
- Windows now has REAPER evidence on the Windows 11 ARM64 VM with real legacy
  plugins:
  - APC `x64`
  - APC `x86`
  - Serum `x64`
  - Serum `x86`
- The Windows lane now includes:
  - discovery / instantiate
  - embedded UI open / close
  - short transport / audio proof
  - four-plugin coexistence in one REAPER session
- Remaining Windows caveat:
  - this is still a secondary Windows 11 ARM64 VM + REAPER x64 lane, not the
    primary supported release claim for `v0.1-alpha`

### GUI/editor behavior

- macOS plugin GUI handling is strongest in REAPER today.
- On macOS, the supported bridged-editor posture is now the bridge-owned live
  editor window. This is the path validated in the strongest current lane for
  Serum, APC, and Khords.
- Bridged `x64` VST2 live windows on macOS now also carry the Keepsake strap
  inside the same window above the plugin UI. Treat that as part of the live
  baseline, not as evidence for embedded interaction.
- The old IOSurface embedded preview path still exists in tree, but it should
  be treated as diagnostic / experimental only and is now intended for
  operator/debug use rather than normal alpha configuration. The release should
  not imply that interactive embedded editing is a settled or generally
  supported macOS mode.
- AU GUI handling is not yet part of the strongest proven alpha lane.
- On Windows VST2, plugin-handle resizing is the supported posture for
  resizable editors. Host-edge resize for embedded bridged editors should still
  be treated as unsupported / experimental.

### Packaging and install surface

- `v0.1-alpha` is now published:
  - `https://github.com/infinite-loop-audio/keepsake/releases/tag/v0.1-alpha`
- macOS is the primary supported release artifact.
- Windows and Linux assets are published as explicitly experimental attachments.
- Signed/notarized vs unsigned macOS alpha distribution remains an open caveat,
  not a settled user promise.
- Build-from-source remains maintainer-oriented, but the release/install
  surface for `v0.1-alpha` is now documented and packaged around the candidate
  artifact flow.

## Release Rule

If a release claim conflicts with this file, either:

1. validate the lane and update this file, or
2. narrow the claim

Do not publish around unresolved ambiguity.

## Next Task

Use this file as the post-release claim boundary for `v0.1-alpha`: correct the
docs when evidence improves, but do not silently widen support claims without
updating the release surface.
