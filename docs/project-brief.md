# Keepsake — Project Brief

> A standalone open-source CLAP plugin that bridges legacy plugins — VST2, VST3, and AU v2, including 32-bit binaries — into CLAP-capable hosts.

Published by [Infinite Loop Audio](https://github.com/infinite-loop-audio) under the GNU Lesser General Public License v2.1.

---

## What Keepsake is

Keepsake is a CLAP plugin that internally loads and runs legacy plugins using format-specific loaders: VeSTige (clean-room) for VST2, the VST3 SDK for VST3, and Apple's AudioToolbox for AU v2. It exposes each discovered plugin as a distinct CLAP plugin entry via the CLAP plugin factory, so that any CLAP-capable host sees them as ordinary CLAP plugins — with their correct names, vendors, categories, and feature tags.

Each plugin runs in an isolated subprocess, providing crash isolation and bitness bridging — 32-bit plugins run in a 32-bit helper process while the host stays 64-bit.

The user installs Keepsake once. Their CLAP host scans it, and their legacy plugins appear alongside everything else in the plugin browser.

---

## Why it exists

Signal (the audio engine behind Loophole) already hosts CLAP, VST3, AU, and LV2 natively via clean-room Rust implementations. VST2 cannot be added to Signal directly — Steinberg discontinued the VST2 SDK in October 2018, closed new license agreements, and has explicitly stated that new VST2 host products are not permitted without a pre-2018 signed license. Signal does not hold one.

The user need is real and goes beyond VST2:

- **VST2 plugins** — many useful legacy plugins exist only in VST2 format and will never be ported.
- **32-bit plugins** — all formats, increasingly orphaned as hosts and OSes drop 32-bit support.
- **Crash-prone plugins** — any format, where process isolation prevents a bad plugin from taking down the host session.

Keepsake solves this cleanly:

- **Signal ships zero legacy bridge code.** It hosts CLAP. Keepsake is a CLAP plugin. Signal's relationship with each format stays at its own native hosting boundary.
- **Keepsake is a standalone open-source project.** It is not bundled with Signal or distributed by Infinite Loop Audio as part of any commercial product.
- **Users self-install.** The legal exposure stays with the open-source project and its format-specific lineage, not with Signal or Loophole.

---

## Legal basis and precedent

### VeSTige

Keepsake uses [VeSTige](https://web.archive.org/web/*/https://github.com/LMMS/lmms/blob/master/plugins/vst_base/vestige/aeffect.h) — a clean-room reverse-engineered header implementing the VST2 ABI, originally written by Javier Serrano Polo. VeSTige is licensed under LGPL v2.1 and has been used by LMMS for over 20 years without successful legal challenge. Ardour also uses it. Keepsake's licence (LGPL v2.1) matches the VeSTige lineage.

### Why not the Steinberg VST2 SDK

The Steinberg VST2 SDK is not used, not referenced, and not redistributed. Steinberg:

- Discontinued the VST2 SDK in October 2018
- Closed all new license agreements at that date
- Has stated on record: *"new products supporting VST2 are not allowed"*
- Has stated: *"A licence agreement is also required if the only use of the VST2 technology is for internal purposes"*
- Has issued DMCA takedowns for VST2 SDK distribution (though not successfully against VeSTige specifically)

Sources:
- https://forums.steinberg.net/t/vst-2-sdk-discontinued/201774
- https://forums.steinberg.net/t/can-i-create-vst-2-support-for-host-application-vst-3-license/202012
- https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Licensing.html

### Open-source precedent

LMMS and Ardour have both shipped VeSTige-based VST2 hosting for over 20 years under GPL/LGPL licences without successful enforcement action. This is the established open-source pattern that Keepsake follows.

The RustAudio/vst-rs crate — the Rust community's canonical VST2 reference — was archived in March 2024 with the explicit note that it is no longer possible to acquire a license to distribute VST2 products. The Rust audio ecosystem has conceded the commercial hosting question. Keepsake is not a commercial host.

### Trademark

"VST" is a registered trademark of Steinberg Media Technologies GmbH (US registrations 75584165 and 79047426; EU EUIPO 000763367). Keepsake does not use the VST Compatible logo, does not claim Steinberg certification, and does not use "VST" as part of its name. References to "the VST2 binary plugin format" in documentation are descriptive and nominative only. All uses include the attribution: *VST is a registered trademark of Steinberg Media Technologies GmbH.*

### The VST3 licence exclusion

The VST3 SDK licence explicitly states: *"This Agreement neither applies to the development nor the hosting of VST2 Plug-Ins."* A VST3 plugin wrapper for VST2 hosting would therefore create a licence conflict. Keepsake uses CLAP as its outer format — which is MIT-licensed and has no such exclusion — specifically to avoid this problem.

---

## Architecture

### CLAP plugin factory model

A single `.clap` binary exposes a plugin factory that returns one descriptor per discovered legacy plugin. Each descriptor carries the plugin's own name, vendor, version, and feature tags (mapped from the source format's metadata to CLAP feature strings).

From the host's perspective, every bridged plugin appears as a distinct, named CLAP plugin. No special host support is required beyond standard CLAP scanning.

```
keepsake.clap
  └─ factory
       ├─ "Vintage Synth X"     (id: keepsake.vst2.1234ABCD, vendor: Acme, features: [instrument])
       ├─ "Classic Compressor"  (id: keepsake.vst3.AABB...., vendor: Acme, features: [audio-effect])
       └─ "Old Reverb"          (id: keepsake.au.RvbXAcme,   vendor: Whoever, features: [audio-effect, reverb])
```

### Out-of-process hosting

Keepsake loads each plugin in an isolated subprocess. This provides:

- **Crash isolation:** a crashed plugin produces silence and an error state rather than taking down the session.
- **Bitness bridging:** 32-bit plugins run in a 32-bit helper process (`keepsake-bridge-32`) while the host stays 64-bit. 64-bit plugins use `keepsake-bridge-64`.

### Format loaders

| Format | ABI surface | Licence | Platforms | Notes |
|---|---|---|---|---|
| VST2 | VeSTige (clean-room) | LGPL v2.1 | macOS, Windows, Linux | No Steinberg SDK |
| VST3 | VST3 SDK | GPLv3 or proprietary | macOS, Windows, Linux | Runs in subprocess; license at process boundary |
| AU v2 | AudioToolbox | macOS system framework | macOS only | Component Manager AU; AUv3 deferred |

### Scan and cache

Keepsake scans configured plugin paths per format at startup and caches results so the CLAP factory can respond immediately without blocking the host's scan cycle. A rescan can be triggered via a Keepsake configuration utility or preferences entry.

---

## Signal / Loophole integration

Signal integrates with Keepsake as a well-behaved CLAP host — no special code is required for basic functionality, since VST2 plugins simply appear as CLAP plugins in the scan results.

Optional deeper integration (to be implemented separately in Signal) can include:

| Tier | Feature | Signal changes required |
|---|---|---|
| 1 | VST2 plugins appear in plugin browser with correct names | None — works via CLAP factory |
| 2 | Detect Keepsake presence by known plugin ID | Small — one ID check at scan time |
| 2 | "Rescan VST2 plugins" trigger in Signal settings | Small — calls Keepsake rescan if bridge detected |
| 2 | "Legacy / VST2" badge on bridge-sourced plugins | Small — flag on plugin metadata |
| 2 | First-run hint if VST2 files found but Keepsake not installed | Small — detection + one-time notification |
| 3 | VST2 scan path configuration in Signal preferences | Medium — forwarded to Keepsake config |
| 3 | Plugin browser grouping/filtering by legacy format | Medium — browser data model |
| 3 | Crash isolation reporting if bridge process goes down | Medium — CLAP error state surfacing |

Keepsake's CLAP plugin ID namespace (`keepsake.<format>.*`) is stable and intended for Signal to use as a detection key. The `keepsake.*` prefix identifies all bridged plugins; the format segment (`vst2`, `vst3`, `au`) distinguishes the source format.

---

## Implementation notes

### Language and dependencies

- **C or C++** — VeSTige is a C header; the project does not need to be in Rust
- **VeSTige** — LGPL v2.1, drop-in header for VST2 ABI, no Steinberg SDK required
- **VST3 SDK** — GPLv3 or proprietary, for VST3 hosting; runs in subprocess to isolate license
- **AudioToolbox** — macOS system framework, for AU v2 hosting
- **CLAP SDK** — MIT licensed, no complications
- **No Steinberg VST2 SDK** — not used, not referenced, not present in the repository

### Prior art to study

- [LMMS VeSTige integration](https://github.com/LMMS/lmms) — 20+ year reference implementation
- [Carla](https://github.com/falkTX/Carla) — multi-format plugin host that exposes bridged plugins via CLAP/LV2 factory; closest architectural reference for the factory model
- [Ardour VST2 support](https://github.com/Ardour/ardour) — another VeSTige-lineage reference

### Platform targets

- macOS (x86_64 and arm64)
- Windows (x86_64)
- Linux (x86_64)

VST2 on Apple Silicon macOS requires Rosetta 2 for x86_64 VST2 binaries, or native arm64 VST2 plugins where they exist. This is a known limitation of VST2 generally.

---

## What Keepsake is not

- It is not a commercial product and is not sold
- It is not affiliated with or endorsed by Steinberg Media Technologies
- It is not a replacement for proper CLAP ports of legacy plugins
- It does not bundle the Steinberg VST2 SDK or any Steinberg intellectual property
- It is not part of the Signal or Loophole commercial distribution

---

## Name

*Keepsake: something kept or given to be kept as a memento.*

The name reflects the intent — these plugins are not legacy baggage to be tolerated, but tools with genuine value that deserve to keep working. Checked clear of trademark and product conflicts in the audio software space (April 2026). The one notable clash considered and ruled out was "Heirloom" by Spitfire Audio (a commercial orchestral instrument plugin, 2022).

*VST is a registered trademark of Steinberg Media Technologies GmbH.*
