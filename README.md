# Keepsake

**A CLAP plugin that gives your legacy plugins a home in modern hosts.**

Keepsake bridges the gap between legacy plugin formats — VST2, VST3, AU v2, including 32-bit binaries — and [CLAP](https://cleveraudio.org/), the open plugin standard supported by an increasing number of modern hosts. Install it once, and your old plugins appear in your plugin browser alongside everything else, with their correct names, vendors, and categories. Each plugin runs in its own isolated process for crash safety and 32-to-64-bit bridging. No special host support required.

---

## The problem it solves

Legacy plugins are getting left behind. VST2 is dead as a living standard — Steinberg discontinued the SDK in 2018 and closed all new license agreements. 32-bit plugins are orphaned as hosts and operating systems drop 32-bit support. Crash-prone plugins can take down an entire session.

But the plugins aren't dead. Synths, compressors, and effects — some of them irreplaceable, some of them made by developers who will never port them — still work, still sound good, and still deserve to run.

Keepsake is for those plugins.

---

## How it works

Keepsake is a standard `.clap` plugin file. When your host scans it, it doesn't just appear as a single plugin — it exposes every legacy plugin it has found (VST2, VST3, AU v2) as its own distinct CLAP plugin entry, complete with the original plugin's name, vendor, version, and feature tags.

From your host's perspective, your legacy plugins are CLAP plugins. Loading one is seamless. There is no special workflow.

Each plugin runs in an isolated subprocess, so a crash in a legacy plugin produces silence and an error state rather than taking down your session. 32-bit plugins run in a 32-bit helper process, bridged to your 64-bit host automatically.

---

## Requirements

- A CLAP-capable host (version 1.0 or later)
- Legacy plugins you want to bridge (VST2, VST3, or AU v2)
- macOS 11+, Windows 10+, or Linux (x86_64)

> **Apple Silicon note:** x86_64 plugins require Rosetta 2. Native arm64 plugins run natively.
>
> **32-bit note:** 32-bit plugins are supported on Windows (via WoW64) and Linux (via multilib). macOS 10.15+ dropped 32-bit support entirely — this is a platform limitation.

---

## Installation

> Keepsake is in early development. Binary releases will be available here once the project reaches a stable state.

Until then, see [Building from source](#building-from-source) below.

Once built or downloaded, place `keepsake.clap` in your system CLAP plugin folder:

| Platform | Path |
|---|---|
| macOS | `~/Library/Audio/Plug-Ins/CLAP/` or `/Library/Audio/Plug-Ins/CLAP/` |
| Windows | `%COMMONPROGRAMFILES%\CLAP\` or `%LOCALAPPDATA%\Programs\Common\CLAP\` |
| Linux | `~/.clap/` or `/usr/lib/clap/` |

Rescan plugins in your host. Your VST2 plugins will appear.

---

## Configuring scan paths

On first load, Keepsake scans the standard plugin locations for each format on your platform. You can configure additional scan paths — and trigger a manual rescan — via Keepsake's settings panel or a configuration file at:

| Platform | Path |
|---|---|
| macOS | `~/Library/Application Support/Keepsake/config.toml` |
| Windows | `%APPDATA%\Keepsake\config.toml` |
| Linux | `~/.config/keepsake/config.toml` |

> Configuration file format will be documented here once stable.

---

## Docs

Project docs, planning, and architecture decisions live in [`docs/README.md`](docs/README.md).

---

## Signal / Loophole

Keepsake was created by [Infinite Loop Audio](https://github.com/infinite-loop-audio), the team behind [Signal](https://github.com/infinite-loop-audio/signal) and Loophole. Signal supports CLAP natively, which means Keepsake works in Signal out of the box — your VST2 plugins appear in the plugin browser with no additional setup.

**Keepsake is a separate, standalone open-source project.** Signal does not ship VST2 support and does not include any VST2 code. Keepsake is not bundled with Signal or distributed as part of any Infinite Loop Audio commercial product. It exists as a community tool for users who need it, and is published here separately on that basis.

If you are a developer of another CLAP host and want to offer tighter Keepsake integration (rescan triggers, legacy badges, scan path configuration), the stable plugin ID namespace is `keepsake.<format>.*` (e.g., `keepsake.vst2.*`, `keepsake.vst3.*`, `keepsake.au.*`). See [`docs/project-brief.md`](docs/project-brief.md) for the integration tier details.

---

## Building from source

> Build instructions will be added as the project takes shape. The project is C/C++, depends on [VeSTige](https://github.com/LMMS/lmms/blob/master/plugins/vst_base/vestige/aeffect.h), the [CLAP SDK](https://github.com/free-audio/clap), and optionally the [VST3 SDK](https://github.com/steinbergmedia/vst3sdk), and targets macOS, Windows, and Linux.

---

## Contributing

Contributions are welcome. The project is in its early stages — the most useful contributions right now are:

- Platform testing and bug reports
- VST2 edge case coverage (parameter handling, MIDI, GUI lifecycle)
- Build system improvements
- Documentation

Please open an issue before starting significant work so effort isn't duplicated.

---

## Legal

**VST2:** Keepsake uses [VeSTige](https://github.com/LMMS/lmms/blob/master/plugins/vst_base/vestige/aeffect.h), a clean-room reverse-engineered implementation of the VST2 ABI originally written by Javier Serrano Polo and used by LMMS for over 20 years. The Steinberg VST2 SDK is not used, not referenced, and not present in this repository.

**VST3:** Keepsake hosts VST3 plugins using the [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) (GPLv3 or proprietary license). The VST3 loader runs in a separate subprocess, placing the license boundary at the process/IPC edge.

**AU v2:** Keepsake hosts Audio Unit v2 plugins on macOS using Apple's AudioToolbox system framework.

Keepsake is not affiliated with, endorsed by, or certified by Steinberg Media Technologies GmbH. It does not carry the VST Compatible mark.

*VST is a registered trademark of Steinberg Media Technologies GmbH.*

---

## Licence

GNU Lesser General Public License v2.1 — see [`LICENSE`](LICENSE) for the full text.

This licence matches the VeSTige lineage. You are free to use, modify, and distribute Keepsake under the same terms. If you incorporate Keepsake into a non-LGPL product, the LGPL terms apply to the Keepsake components.
