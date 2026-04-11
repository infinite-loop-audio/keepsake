# G01.014 — Scan Robustness

Status: complete
Owner: Infinite Loop Audio
Updated: 2026-04-11
Governing refs:
  - docs/contracts/002-clap-factory-interface.md
Auto-continuation: allowed within g01

## Scope

Make plugin scanning crash-safe and fix VST3 controller initialization.
Currently, VST2 scanning loads plugins in-process — a crashing plugin
(BFDPlayer, Loopcloud, etc.) can take down the entire scan. VST3 plugins
sometimes report 0 parameters because the edit controller needs separate
initialization.

## Steps

### 1. Subprocess-based VST2 scanning

Instead of loading VST2 plugins in-process for metadata extraction,
scan all plugins via bridge subprocesses:

- Spawn one bridge process for scanning (reuse the existing multi-instance
  bridge)
- Send INIT for each plugin, read metadata from the OK response
- If the bridge crashes on a specific plugin, note it as "crash-prone",
  restart the bridge, continue scanning
- Plugins that crash during scan get auto-flagged for `per-instance`
  isolation

This replaces the in-process `vst2_load_metadata()` for the main scan
path. The in-process loader can remain for fallback or testing.

Benefits:
- No more scan crashes taking down the CLAP factory
- Crash-prone plugins automatically detected and isolated
- Scanning can be parallelized (multiple bridge processes)

### 2. VST3 controller initialization

Fix the VST3 loader to properly initialize the edit controller:

- After creating IComponent, query `getControllerClassId()`
- Create the IEditController as a separate instance from the factory
- Call `controller->initialize()` with a proper host context
- For single-component plugins (where component == controller), handle
  the `queryInterface` path correctly
- Connect component and controller state via `setComponentState()`

This should fix plugins like Pigments reporting 0 parameters.

### 3. Scan timeout

Add a per-plugin scan timeout (e.g., 30 seconds). If a plugin takes
longer than this to respond to INIT, skip it and log a warning.
Some plugins do heavy network requests or license checks at load time.

### 4. Crash history in cache

Extend the cache format to record:
- Plugins that crashed during scan (flagged, not cached as valid)
- Number of consecutive scan failures
- Auto-isolation recommendation

## Evidence Requirements

- Full system scan completes without crashes (previously crashed on
  BFDPlayer, Loopcloud, etc.)
- VST3 plugins report correct parameter counts
- Crash-prone plugins flagged in scan output
