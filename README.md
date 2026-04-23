# Keepsake

**A CLAP plugin that gives your legacy plugins a home in modern hosts.**

Keepsake bridges the gap between legacy plugin formats — VST2, VST3, AU v2, including 32-bit binaries — and [CLAP](https://cleveraudio.org/), the open plugin standard supported by an increasing number of modern hosts. Install it once, and your old plugins appear in your plugin browser alongside everything else, with their correct names, vendors, and categories. Each plugin runs in its own isolated process for crash safety and 32-to-64-bit bridging. No special host support required.

---

## Current Status

Keepsake is in an active alpha-release hardening phase.

The strongest current evidence lane is:

- macOS
- REAPER
- VST2 bridging
- real-plugin validation with APC, Serum, and Khords

Current alpha posture:

- **Primary validated lane:** macOS + REAPER + VST2
- **Experimental / lightly proven:** Windows, Linux, VST3, AU v2, 32-bit

Experimental here means code exists, CI may pass, and individual paths may
work, but the support claim is not yet broad enough to present as settled user
expectation without more release-window validation.

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

Download `v0.1-alpha` from the [releases page](https://github.com/infinite-loop-audio/keepsake/releases/tag/v0.1-alpha) and follow the setup guide for your platform:

- **[macOS setup guide](docs/setup/macos.md)** — primary validated lane
- **[Windows setup guide](docs/setup/windows.md)** — experimental
- **[Linux setup guide](docs/setup/linux.md)** — experimental

The short version: unpack the archive, place `keepsake.clap` (plus the adjacent bridge binary on Windows/Linux) into your CLAP plugin folder, and rescan in your host.

| Platform | CLAP folder |
|---|---|
| macOS | `~/Library/Audio/Plug-Ins/CLAP/` |
| Windows | `%COMMONPROGRAMFILES%\CLAP\` or `%LOCALAPPDATA%\Programs\Common\CLAP\` |
| Linux | `~/.clap/` or `/usr/lib/clap/` |

---

## Configuration

On first load, Keepsake scans the standard VST2 plugin locations for your platform. No config file is required to get started.

To add custom scan paths, change what gets exposed, or adjust isolation settings, create a `config.toml` at:

| Platform | Path |
|---|---|
| macOS | `~/Library/Application Support/Keepsake/config.toml` |
| Windows | `%APPDATA%\Keepsake\config.toml` |
| Linux | `~/.config/keepsake/config.toml` |

Quick example:

```toml
[scan]
vst2_paths = ["/extra/vst/folder"]

[expose]
vst2_bridged = true    # bridge-required VST2 — on by default
vst2_native = false    # native VST2 — off by default

[isolation]
default = "per-instance"
```

Full documentation of every config key: **[config reference](docs/setup/config-reference.md)**

Known limitations and support caveats: **[`docs/known-issues-v0.1-alpha.md`](docs/known-issues-v0.1-alpha.md)**

---

## Docs

- **[Setup guides](docs/setup/README.md)** — installation for macOS, Windows, Linux
- **[Config reference](docs/setup/config-reference.md)** — all config.toml options
- **[Troubleshooting](docs/setup/troubleshooting.md)** — common problems and fixes
- **[Known issues — v0.1-alpha](docs/known-issues-v0.1-alpha.md)** — current alpha caveats
- **[Architecture and planning](docs/README.md)** — project docs, decisions, roadmaps

---

## Signal / Loophole

Keepsake was created by [Infinite Loop Audio](https://github.com/infinite-loop-audio), the team behind [Signal](https://github.com/infinite-loop-audio/signal) and Loophole. Signal supports CLAP natively, which means Keepsake works in Signal out of the box — your VST2 plugins appear in the plugin browser with no additional setup.

**Keepsake is a separate, standalone open-source project.** Signal does not ship VST2 support and does not include any VST2 code. Keepsake is not bundled with Signal or distributed as part of any Infinite Loop Audio commercial product. It exists as a community tool for users who need it, and is published here separately on that basis.

If you are a developer of another CLAP host and want to offer tighter Keepsake integration (rescan triggers, legacy badges, scan path configuration), the stable plugin ID namespace is `keepsake.<format>.*` (e.g., `keepsake.vst2.*`, `keepsake.vst3.*`, `keepsake.au.*`). See [`docs/project-brief.md`](docs/project-brief.md) for the integration tier details.

---

## Building from source

**Requirements:** CMake 3.24+, a C++20 compiler (clang or gcc), and git. External dependencies (CLAP SDK, VST3 pluginterfaces) are fetched automatically by CMake. On macOS, Xcode command line tools are required.

```sh
cmake --preset default
cmake --build build
effigy qa
```

Primary outputs:
- macOS: `build/keepsake.clap` (bundle with helper binaries under `Contents/Helpers/`)
- Windows/Linux: `build/keepsake.clap` + adjacent `keepsake-bridge` binary

Other useful commands:

```sh
effigy doctor          # health check — verify build environment
effigy demo list       # list available demo proofs
effigy demo:supported-proof  # run the primary validation suite
effigy demo run <id>   # run a specific demo
```

The project uses [VeSTige](https://github.com/LMMS/lmms/blob/master/plugins/vst_base/vestige/aeffect.h) (bundled in `vendor/`) for VST2 — not the Steinberg SDK. The [CLAP SDK](https://github.com/free-audio/clap) and VST3 pluginterfaces are fetched via CMake FetchContent. Full build instructions for all three platforms, including 32-bit bridge builds and troubleshooting: **[docs/setup/building.md](docs/setup/building.md)**

---

## Contributing

Contributions are welcome. See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the development workflow, legal constraints (VeSTige-only for VST2 — this matters), and what makes a useful PR. Open an issue before starting significant work so effort isn't duplicated.

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
