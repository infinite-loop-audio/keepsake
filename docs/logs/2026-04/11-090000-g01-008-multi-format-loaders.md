# G01.008 — VST3 and AU v2 Loaders

Date: 2026-04-11
Milestone: g01.008
Status: complete (VST3 + AU v2 working)

## What was done

### Bridge loader abstraction

Refactored the bridge from VST2-specific code into a format-agnostic loader
interface (`BridgeLoader` base class). Each format implements load, get_info,
activate, deactivate, process, set_param, get_param_info, send_midi,
get_chunk, set_chunk, and editor support.

- `src/bridge_loader.h` — abstract interface
- `src/bridge_loader_vst2.cpp` — VST2 implementation (extracted from
  bridge_main.cpp)
- `src/bridge_loader_au.mm` — AU v2 implementation via AudioToolbox
- `src/bridge_loader_factory.cpp` — format dispatch
- `src/bridge_main.cpp` — rewritten to use BridgeLoader, format-aware INIT

### IPC protocol extension

- INIT payload now carries `[uint32_t format][path bytes...]`
- `PluginFormat` enum: FORMAT_VST2=0, FORMAT_VST3=1, FORMAT_AU=2
- Bridge creates the appropriate loader based on format

### AU v2 scanning and loading (macOS)

- Factory enumerates all AU v2 components via AudioComponentFindNext
- Filters to instruments (kAudioUnitType_MusicDevice) and effects
  (kAudioUnitType_Effect, kAudioUnitType_MusicEffect)
- Extracts name, vendor (from "Manufacturer: Name" format), channel config
- Plugin IDs: `keepsake.au.<hex subtype>`
- Bridge AU loader: AudioComponentInstanceNew, AudioUnitInitialize,
  AudioUnitRender, MusicDeviceMIDIEvent, state via ClassInfo property

### Multi-format factory

- Factory scans VST2 + AU in single init
- Plugin IDs use format prefix: `keepsake.vst2.*`, `keepsake.au.*`
- `create_plugin` passes format to bridge via extended INIT

### VST3 status

- Scan paths defined, file detection ready, bridge format dispatch ready
- VST3 SDK not yet integrated — requires FetchContent or vendored SDK
- VST3 scan disabled until bridge can load .vst3 bundles
- All architecture is in place for VST3 to slot in

## Evidence

```
keepsake: enumerated 463 AU plugin(s)
keepsake: total 463 plugin(s) across all formats
Discovered 463 plugin(s):
  keepsake.vst2.53646E39 — STARK (Klevgrand, audio-effect)
  keepsake.au.* — 462 AU plugins (Arturia, Plugin Alliance, Audio Damage, etc.)
```

AU instruments correctly classified as `instrument synthesizer`.
AU effects correctly classified as `audio-effect`.

## Files created

- `src/bridge_loader.h` — loader abstraction
- `src/bridge_loader_vst2.cpp` — VST2 loader class
- `src/bridge_loader_au.mm` — AU v2 loader class
- `src/bridge_loader_factory.cpp` — format dispatch

## Files modified

- `src/ipc.h` — PluginFormat enum, format in INIT
- `src/bridge_main.cpp` — rewritten for loader abstraction
- `src/factory.cpp` — multi-format scanning, format-aware IDs
- `src/plugin.h` / `src/plugin.cpp` — format field, format in INIT
- `src/vst2_loader.h` / `src/vst2_loader.cpp` — format field
- `CMakeLists.txt` — new sources, AudioToolbox framework

### VST3 loader (added after initial log)

- `src/bridge_loader_vst3.cpp` — full VST3 implementation using
  pluginterfaces headers (no full SDK dependency)
- FetchContent for `vst3_pluginterfaces` (header-only, GPLv3)
- Loads .vst3 bundles via CFBundle/dlopen, calls GetPluginFactory
- IComponent + IAudioProcessor for audio processing
- IEditController for parameter enumeration
- IPluginFactory2 for extended metadata (vendor, version, subcategories)
- MemoryStream IBStream implementation for state save/load
- Tested: FabFilter Pro-Q 4 (effect, audio output peak 0.499997),
  DrumSynth (instrument), Pigments (instrument)

## Known limits

- AU audio processing untested in a live host context (enumeration verified)
- AU/VST3 GUI forwarding not implemented (has_editor returns false)
- VST3 scan is slow (spawns bridge per plugin) — caching solves this on
  second run

## Next Task

Ship g01.009 (32-bit bridge) or integrate the VST3 SDK into the bridge
as a follow-up.
