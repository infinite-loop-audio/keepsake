# G01.004 — Scan Cache and Configuration

Status: complete
Owner: Infinite Loop Audio
Updated: 2026-04-10
Governing refs:
  - docs/contracts/001-working-rules.md
  - docs/contracts/002-clap-factory-interface.md
  - docs/architecture/system-architecture.md
Auto-continuation: allowed within g01

## Scope

Add scan result caching and a `config.toml` configuration file so that
Keepsake does not rescan every VST2 directory on every host startup, and so
that users can configure additional scan paths and trigger rescans.

This is the last piece needed to close g01's sequencing intent: "scanning,
caching, and rescan triggering work at a basic level."

This milestone does **not** include:
- GUI / editor forwarding
- Plugin state save/restore
- Full MIDI support
- CI for Windows/Linux
- 32-bit bridge binaries

## Steps

### 1. Configuration file

Define and implement `config.toml` at platform-standard locations:

| Platform | Path |
|---|---|
| macOS | `~/Library/Application Support/Keepsake/config.toml` |
| Windows | `%APPDATA%\Keepsake\config.toml` |
| Linux | `~/.config/keepsake/config.toml` |

Schema:

```toml
# Additional VST2 scan paths (appended to platform defaults)
[scan]
vst2_paths = [
    "/custom/path/to/vst2/plugins",
]

# Set to true to force a rescan on next host startup
rescan = false
```

The file is optional. If missing, platform defaults are used.
`KEEPSAKE_VST2_PATH` environment variable still overrides everything.

Acceptance:
- Config file is read at init time
- Custom paths are scanned alongside platform defaults
- Missing config file is handled gracefully (no error)

### 2. Scan result cache

Cache scan results to a JSON file so subsequent startups are instant:

| Platform | Path |
|---|---|
| macOS | `~/Library/Application Support/Keepsake/cache.json` |
| Windows | `%APPDATA%\Keepsake\cache.json` |
| Linux | `~/.config/keepsake/cache.json` |

Cache entries per plugin:

```json
{
  "path": "/Library/Audio/Plug-Ins/VST/Serum.vst",
  "mtime": 1712784000,
  "unique_id": 1482756952,
  "name": "Serum",
  "vendor": "Xfer Records",
  "vendor_version": 66313,
  "category": 2,
  "flags": 305,
  "num_inputs": 0,
  "num_outputs": 2,
  "needs_cross_arch": true
}
```

Invalidation rules:
- If the plugin file's modification time has changed, rescan it
- If the plugin file no longer exists, remove from cache
- If `config.toml` has `rescan = true`, clear the cache and rescan everything
  (then set `rescan = false`)
- If `KEEPSAKE_VST2_PATH` is set, bypass the cache entirely (dev/testing mode)

Acceptance:
- Second startup with no plugin changes loads instantly from cache
- Adding a new plugin to the scan path is detected on next startup
- Removing a plugin removes it from the cache
- `rescan = true` in config forces a full rescan

### 3. Rescan trigger

Provide a way to trigger a rescan without editing the config file:

- Create a sentinel file `rescan` (no extension) in the Keepsake config
  directory. If present at startup, Keepsake clears the cache and rescans,
  then deletes the sentinel.
- This gives CLAP hosts a simple mechanism to trigger a rescan: write the
  sentinel file, then request a plugin rescan.

Acceptance:
- Creating the sentinel file causes a rescan on next init
- The sentinel is removed after the rescan

### 4. Evidence collection

- Measure startup time with and without cache (before/after)
- Verify cache invalidation works (modify a plugin, observe rescan)
- Record in `docs/logs/`

## Stop Conditions

- Stop if JSON parsing adds an unacceptable dependency (consider a minimal
  parser or hand-written format)
- Stop if cache file corruption causes startup failures (must handle
  gracefully)

## Evidence Requirements

- Startup time comparison: cached vs uncached
- Cache invalidation working correctly
- Config file scan paths working

## Next Task

After this milestone: g01 sequencing intent is met. Assess whether to close
g01 and open g02 for polish work (GUI, full MIDI, state, CI), or continue
adding milestones to g01.
