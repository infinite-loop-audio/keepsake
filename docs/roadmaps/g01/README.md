# G01 — Initial Build and Proof-of-Concept

Status: planning
Started: 2026-04-10

## Milestones

| Milestone | Title | Status |
|---|---|---|
| `001` | [CLAP Factory and VeSTige Loader PoC](001-clap-factory-and-vst2-loader-poc.md) | complete |
| `002` | [Audio Bridge and Subprocess Isolation](002-audio-bridge-and-subprocess-isolation.md) | complete |
| `003` | [Cross-Platform and Cross-Architecture](003-cross-platform-and-cross-architecture.md) | complete |
| `004` | [Scan Cache and Configuration](004-scan-cache-and-configuration.md) | complete |
| `005` | [MIDI, Parameter Automation, and State](005-midi-params-and-state.md) | complete |
| `006` | [GUI Forwarding](006-gui-forwarding.md) | complete |
| `007` | [CI and Cross-Platform Testing](007-ci-and-cross-platform-testing.md) | complete |
| `008` | [VST3 and AU v2 Loaders](008-vst3-and-au-loaders.md) | complete |
| `009` | [32-Bit Bridge Binaries](009-32-bit-bridge.md) | complete |
| `010` | [Configurable Process Isolation](010-configurable-process-isolation.md) | complete |
| `011` | [Editor Header Bar](011-editor-header-bar.md) | complete |
| `012` | [Windows and Linux GUI](012-windows-linux-gui.md) | complete |
| `013` | [Embedded Editors](013-embedded-editors.md) | complete |
| `014` | [Scan Robustness](014-scan-robustness.md) | complete |
| `015` | [Codebase Health](015-codebase-health.md) | planned |
| `016` | [Soundcheck Integration](016-soundcheck-integration.md) | deferred |

## Sequencing Intent

G01 covers the work from zero to a production-capable multi-format plugin
bridge. The initial proof-of-concept targets (001–004) are met. The remaining
milestones (005–009) take the bridge from "it works" to "it's usable for real
music production."

G01 ends when bridged plugins are fully playable, automatable, saveable, and
visible with GUIs in a CLAP host, across all three platforms, with support for
VST2, VST3, and AU v2 formats including 32-bit plugins.

## Next Task

Ship the next ready milestone. Suggested order: 014 (scan robustness) → 011
(header bar) → 015 (codebase health) → 007 (CI) → 012 (Windows/Linux GUI)
→ 013 (embedded editors) → 016 (Soundcheck integration, when ready).
