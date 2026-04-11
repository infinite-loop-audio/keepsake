# 001 - Keepsake Vision

Status: active
Owner: Infinite Loop Audio
Updated: 2026-04-10
Vision refs: this document is the primary vision artifact

## Long-Term Outcome

A stable, cross-platform, open-source CLAP plugin that gives legacy plugins a
working home in any modern CLAP-capable host. VST2, VST3, and AU v2 plugins —
including 32-bit binaries — appear as ordinary CLAP plugins with correct
metadata, crash-isolated in their own processes. Users install it once. The
rest is transparent.

## Why This Exists

Legacy plugin formats are either dead or increasingly awkward to host directly:

- **VST2** — Steinberg discontinued the SDK in 2018, closed all new license
  agreements, and is actively phasing it out. But the plugins aren't dead.
- **32-bit plugins** — all formats, increasingly orphaned as hosts and OSes
  drop 32-bit support. Many valuable tools will never get 64-bit ports.
- **AU v2 / VST3 in hostile environments** — crash-prone plugins, or plugins
  a host cannot load natively, still deserve to run without taking down a
  session.

Signal (the audio engine behind Loophole) hosts CLAP natively but cannot host
VST2 directly — Steinberg's explicit position is that new host products
supporting VST2 require a pre-2018 signed license. Signal does not hold one.

Keepsake solves this cleanly:

- Signal ships zero legacy format code. It hosts CLAP. Keepsake is a CLAP
  plugin. Signal never touches VST2, and its relationship with VST3 and AU
  stays at its own native hosting boundary.
- Keepsake is a standalone open-source project, not part of any commercial
  distribution.
- Users self-install. Legal exposure stays with the open-source project and its
  format-specific lineage, not with Signal or Loophole.

## Target Operating Model

A single `.clap` binary exposes a plugin factory that returns one descriptor
per discovered legacy plugin — VST2, VST3, and AU v2. Each descriptor carries
the original plugin's name, vendor, version, and feature tags. From the host's
perspective, every bridged plugin is a distinct named CLAP plugin.

Each plugin instance runs in an isolated subprocess. This provides crash
isolation (a crashed plugin produces silence and an error state rather than
taking down the session) and bitness bridging (32-bit plugins run in a 32-bit
subprocess while the host stays 64-bit).

Keepsake scans configured plugin paths at startup, caches results, and can
trigger a rescan via preferences or a config file. Format-specific loaders
handle the ABI differences behind a common bridge interface.

## Strategic Constraints

**Legal non-negotiable (VST2):** Only VeSTige is permitted as the VST2 ABI
surface. The Steinberg VST2 SDK must never appear in this repository. The
VeSTige lineage (LGPL v2.1, 20+ year open-source track record via LMMS and
Ardour) is the established precedent Keepsake follows.

**VST3 SDK licensing:** The VST3 SDK is dual-licensed (GPLv3 or Steinberg
proprietary). The VST3 loader runs in a separate subprocess, placing the
license boundary at the process/IPC boundary. GPLv3 compatibility with the
main LGPL v2.1 binary must be resolved before VST3 support ships.

**AU hosting:** Audio Units are hosted via Apple's public AudioToolbox
framework on macOS. No special licensing concerns. AU v2 (Component Manager)
is the initial target; AU v3 (AUv3 app extensions) is deferred unless a real
need arises.

**32-bit bridging:** The subprocess architecture must support running a 32-bit
bridge helper binary alongside the 64-bit main process from the start. On
macOS, 32-bit binaries require macOS 10.14 or earlier (Catalina dropped 32-bit
support entirely) — this is a platform limitation, not a Keepsake one. On
Windows, WoW64 supports 32-bit processes on 64-bit systems natively.

**Trademark:** "VST" is a registered trademark of Steinberg Media Technologies
GmbH. Keepsake does not use the VST Compatible mark, does not claim Steinberg
certification, and does not use "VST" as part of its name. All uses are
descriptive and nominative. Attribution: *VST is a registered trademark of
Steinberg Media Technologies GmbH.*

**CLAP as outer format:** The VST3 SDK licence explicitly excludes VST2 hosting
use. CLAP (MIT licensed) has no such exclusion. Keepsake's outer format choice
is permanent and non-negotiable for this legal reason.

**Licence:** LGPL v2.1 — matches the VeSTige lineage. The VST3 loader
subprocess may carry GPLv3 terms from the VST3 SDK; the process boundary keeps
this separate from the main binary.

## Platform Targets

- macOS (x86_64 and arm64, Rosetta 2 for x86_64 VST2 on Apple Silicon)
- Windows (x86_64)
- Linux (x86_64)

## Signal / Loophole Integration Posture

Keepsake integrates with Signal as a well-behaved CLAP plugin — no special code
required for basic functionality. Optional deeper integration tiers are defined
in `docs/project-brief.md`. Signal integration work belongs in the Signal repo,
not here.

Keepsake's stable plugin ID namespace (`keepsake.<format>.*`) is the detection
key for Signal and other hosts.

## Success Criteria

- Legacy plugins (VST2, VST3, AU v2) appear in CLAP hosts with correct names,
  vendors, and feature tags, with no special host support required.
- 32-bit plugins run via the bridge on 64-bit hosts.
- Crashes in bridged plugins are isolated — the host session stays up.
- Builds on macOS, Windows, and Linux from a clean checkout.
- No Steinberg VST2 SDK present or referenced. VeSTige only for VST2.
- Published under LGPL v2.1 with full source.

## Risks and Constraints

| Risk | Mitigation |
|---|---|
| Steinberg legal challenge (VST2) | VeSTige lineage + LGPL + no SDK use; established open-source precedent |
| VST3 SDK license compatibility | Subprocess boundary isolates GPLv3; resolve before VST3 ships |
| VST2 ABI edge cases | Study LMMS VeSTige and Carla factory model closely before implementing |
| Cross-platform crash isolation complexity | Subprocess model is established; study Carla's approach |
| 32-bit on macOS | macOS 10.15+ dropped 32-bit entirely; document as platform limitation |
| Apple Silicon Rosetta complexity | This is a VST2/x86_64 limitation, not Keepsake's; document clearly |

## Next Task

Land g01.001 — the first roadmap milestone (CLAP factory and VeSTige loader
PoC) is ready. VST2 is the first format to prove the architecture; VST3 and
AU v2 loaders follow once the factory and subprocess model are working.
