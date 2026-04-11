# G01.008 — VST3 and AU v2 Loaders

Status: complete
Owner: Infinite Loop Audio
Updated: 2026-04-10
Governing refs:
  - docs/contracts/002-clap-factory-interface.md
  - docs/architecture/system-architecture.md
  - docs/vision/001-keepsake-vision.md
Auto-continuation: allowed within g01

## Scope

Add VST3 and AU v2 plugin loading to Keepsake, making it the multi-format
bridge described in the vision. The CLAP factory, subprocess bridge, and
platform abstraction are already format-agnostic — this milestone adds the
format-specific loaders.

## Steps

### 1. VST3 loader

Implement a VST3 plugin loader in the bridge subprocess:

- Use the VST3 SDK (GPLv3) — the bridge subprocess is a separate binary,
  placing the license boundary at the process/IPC edge
- Load VST3 plugins via the VST3 hosting API (`IPluginFactory`,
  `IComponent`, `IAudioProcessor`)
- Extract metadata: name, vendor, version, category, subcategories,
  uniqueID (FUID), channel configuration
- Map VST3 subcategories to CLAP feature strings
- Process audio via `IAudioProcessor::process()`
- Add `keepsake.vst3.<fuid>` plugin ID namespace per contract 002

Platform scan paths:
- macOS: `~/Library/Audio/Plug-Ins/VST3/`, `/Library/Audio/Plug-Ins/VST3/`
- Windows: `%COMMONPROGRAMFILES%\VST3\`
- Linux: `~/.vst3/`, `/usr/lib/vst3/`, `/usr/local/lib/vst3/`

Acceptance:
- A real VST3 plugin appears in the CLAP factory with correct metadata
- Audio processes through the VST3 bridge

### 2. AU v2 loader (macOS only)

Implement an AU v2 plugin loader:

- Use Apple's AudioToolbox framework (`AudioComponentFindNext`,
  `AudioComponentInstanceNew`, `AudioUnitInitialize`, `AudioUnitRender`)
- Extract metadata: name, manufacturer, type/subtype/manufacturer codes
- Map AU types to CLAP features (instrument, effect, etc.)
- Process audio via `AudioUnitRender()`
- Add `keepsake.au.<type><subtype><mfr>` plugin ID namespace per contract 002

Platform scan paths: AudioComponentManager handles discovery automatically.

Acceptance:
- A real AU v2 plugin appears in the CLAP factory with correct metadata
- Audio processes through the AU bridge

### 3. Update contract 002

Add VST3 and AU v2 descriptor mapping tables to the factory interface
contract (currently documented as "to be added when loaders are implemented").

### 4. Update scan cache

Extend the cache format to include VST3 and AU v2 entries alongside VST2.
Add format-specific scan path configuration to config.toml.

### 5. Evidence

- Mixed-format factory output showing VST2, VST3, and AU plugins together
- Audio from a VST3 plugin through the bridge
- Audio from an AU v2 plugin through the bridge

## Stop Conditions

- Stop if VST3 SDK GPLv3 license creates real issues at the process boundary
  (research before implementing)
- Stop if AU v2 AudioUnit API requires main-thread constraints incompatible
  with the bridge subprocess model

## Evidence Requirements

- At least one VST3 and one AU v2 plugin producing audio through the bridge
- Factory shows mixed-format plugin list with correct IDs and features

## Next Task

After this milestone: 32-bit bridge (009) or generation close assessment.
