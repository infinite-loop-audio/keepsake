# 001 - Keepsake Vision

Status: active
Owner: Infinite Loop Audio
Updated: 2026-04-10
Vision refs: this document is the primary vision artifact

## Long-Term Outcome

A stable, cross-platform, open-source CLAP plugin that gives VST2 legacy
plugins a working home in any modern CLAP-capable host. Users install it once.
The rest is transparent.

## Why This Exists

VST2 is dead as a living standard. Steinberg discontinued the SDK in 2018,
closed all new license agreements, and is actively phasing it out. But the
plugins aren't dead — synths, compressors, and effects built in VST2 exist that
will never be ported, and they still work and still deserve to run.

Signal (the audio engine behind Loophole) hosts CLAP natively but cannot host
VST2 directly — Steinberg's explicit position is that new host products
supporting VST2 require a pre-2018 signed license. Signal does not hold one.

Keepsake solves this cleanly:

- Signal ships zero VST2 code. It hosts CLAP. Keepsake is a CLAP plugin.
  Signal never touches VST2.
- Keepsake is a standalone open-source project, not part of any commercial
  distribution.
- Users self-install. Legal exposure stays with the open-source project and its
  VeSTige lineage, not with Signal or Loophole.

## Target Operating Model

A single `.clap` binary exposes a plugin factory that returns one descriptor
per discovered VST2 plugin. Each descriptor carries the original plugin's name,
vendor, version, and feature tags. From the host's perspective, every VST2
plugin is a distinct named CLAP plugin.

VST2 plugins run in isolated subprocesses. A crash produces silence and an
error state rather than taking down the session.

Keepsake scans configured VST2 paths at startup, caches results, and can
trigger a rescan via preferences or a config file.

## Strategic Constraints

**Legal non-negotiable:** Only VeSTige is permitted as the VST2 ABI surface.
The Steinberg VST2 SDK must never appear in this repository. The VeSTige
lineage (LGPL v2.1, 20+ year open-source track record via LMMS and Ardour) is
the established precedent Keepsake follows.

**Trademark:** "VST" is a registered trademark of Steinberg Media Technologies
GmbH. Keepsake does not use the VST Compatible mark, does not claim Steinberg
certification, and does not use "VST" as part of its name. All uses are
descriptive and nominative. Attribution: *VST is a registered trademark of
Steinberg Media Technologies GmbH.*

**CLAP as outer format:** The VST3 SDK licence explicitly excludes VST2 hosting
use. CLAP (MIT licensed) has no such exclusion. Keepsake's outer format choice
is permanent and non-negotiable for this legal reason.

**Licence:** LGPL v2.1 — matches the VeSTige lineage.

## Platform Targets

- macOS (x86_64 and arm64, Rosetta 2 for x86_64 VST2 on Apple Silicon)
- Windows (x86_64)
- Linux (x86_64)

## Signal / Loophole Integration Posture

Keepsake integrates with Signal as a well-behaved CLAP plugin — no special code
required for basic functionality. Optional deeper integration tiers are defined
in `docs/project-brief.md`. Signal integration work belongs in the Signal repo,
not here.

Keepsake's stable plugin ID namespace (`keepsake.vst2.*`) is the detection key
for Signal and other hosts.

## Success Criteria

- VST2 plugins appear in CLAP hosts with correct names, vendors, and feature
  tags, with no special host support required.
- Crashes in VST2 plugins are isolated — the host session stays up.
- Builds on macOS, Windows, and Linux from a clean checkout.
- No Steinberg SDK present or referenced. VeSTige only.
- Published under LGPL v2.1 with full source.

## Risks and Constraints

| Risk | Mitigation |
|---|---|
| Steinberg legal challenge | VeSTige lineage + LGPL + no SDK use; established open-source precedent |
| VST2 ABI edge cases | Study LMMS VeSTige and Carla factory model closely before implementing |
| Cross-platform crash isolation complexity | Subprocess model is established; study Carla's approach |
| Apple Silicon Rosetta complexity | This is a VST2 limitation, not Keepsake's; document clearly |

## Next Task

Author the first roadmap milestone for the initial CLAP plugin factory
proof-of-concept and VeSTige loading scaffold.
