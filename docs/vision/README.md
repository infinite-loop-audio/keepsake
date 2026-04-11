# Vision

## Current Vision

Keepsake makes legacy plugins — VST2, VST3, AU v2, including 32-bit
binaries — first-class citizens in CLAP-capable hosts. Install it once, and
your old plugins appear in your plugin browser alongside everything else —
with their correct names, vendors, and categories. Each runs in its own
isolated process for crash safety and 32-to-64-bit bridging. No special host
support required.

The guiding principle: these plugins are not legacy baggage to be tolerated.
They are tools with genuine value that deserve to keep working.

## Vision Artifacts

- [001-keepsake-vision.md](./001-keepsake-vision.md)

## Constraints

- Keepsake must never use or reference the Steinberg VST2 SDK.
- VeSTige (LGPL v2.1, clean-room ABI implementation) is the only permitted
  VST2 ABI surface.
- VST3 SDK (GPLv3) is permitted for VST3 hosting, in a subprocess only.
- CLAP is the outer plugin format — MIT licensed, no VST3 licence conflicts.
- 32-bit bridging is a first-class architectural concern.
- Keepsake is not a commercial product and is not bundled with Signal or
  Loophole.
- Legal and trademark guardrails from `docs/project-brief.md` are permanent
  constraints, not implementation choices.

## Next Task

Land g01.001 — the CLAP factory and VeSTige loader proof-of-concept. VST2 is
the first format to prove the architecture end-to-end.
