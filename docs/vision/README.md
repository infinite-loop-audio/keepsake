# Vision

## Current Vision

Keepsake makes VST2 legacy plugins a first-class citizen in CLAP-capable
hosts. Install it once, and your old plugins appear in your plugin browser
alongside everything else — with their correct names, vendors, and categories.
No special host support required.

The guiding principle: these plugins are not legacy baggage to be tolerated.
They are tools with genuine value that deserve to keep working.

## Vision Artifacts

- [001-keepsake-vision.md](./001-keepsake-vision.md)

## Constraints

- Keepsake must never use or reference the Steinberg VST2 SDK.
- VeSTige (LGPL v2.1, clean-room ABI implementation) is the only permitted
  VST2 ABI surface.
- CLAP is the outer plugin format — MIT licensed, no VST3 licence conflicts.
- Keepsake is not a commercial product and is not bundled with Signal or
  Loophole.
- Legal and trademark guardrails from `docs/project-brief.md` are permanent
  constraints, not implementation choices.

## Next Task

Author the first roadmap milestone for the initial CLAP plugin factory
proof-of-concept and VeSTige loading scaffold.
