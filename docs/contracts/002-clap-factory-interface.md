# 002 - CLAP Factory Interface

Status: active
Owner: Infinite Loop Audio
Updated: 2026-04-10
Depends on: docs/architecture/system-architecture.md, docs/contracts/001-working-rules.md
Authority owners: Infinite Loop Audio
Affects: g01.001 and all subsequent milestones that touch the factory or descriptor shape

## Problem

Keepsake exposes legacy plugins (VST2, VST3, AU v2) as CLAP plugins through
the CLAP plugin factory interface. The factory must return accurate, stable
descriptors for each discovered plugin so that CLAP hosts can scan, identify,
and instantiate them reliably. Without a contract defining the descriptor
shape, plugin ID namespace, and factory lifecycle rules, implementation will
drift on naming, stability, and host compatibility.

## Contract

### Plugin ID namespace

All Keepsake-generated plugin IDs use the namespace:

```
keepsake.<format>.<uid>
```

Where `<format>` identifies the source plugin format and `<uid>` is derived
from the plugin's native identifier. The `keepsake.*` prefix is the stable
detection key that Signal and other CLAP hosts use to identify bridged plugins
(documented in `docs/project-brief.md`). The format segment prevents collisions
between different format ID schemes and distinguishes when the same plugin
exists in multiple formats.

#### VST2 IDs

```
keepsake.vst2.<uid>
```

`<uid>` is derived from the VST2 plugin's `uniqueID` field (`AEffect.uniqueID`,
a 32-bit integer assigned by the plugin developer). Encoded as an 8-character
zero-padded uppercase hexadecimal string: `keepsake.vst2.1A2B3C4D`.

Edge case: if two VST2 plugins on the same system share the same `uniqueID`
(a known real-world issue), append a disambiguation suffix derived from the
plugin's file path hash: `keepsake.vst2.1A2B3C4D.a3f7`. This suffix is only
added when a collision is detected; non-colliding plugins use the clean form.

#### VST3 IDs

```
keepsake.vst3.<uid>
```

`<uid>` is derived from the VST3 plugin's FUID (a 128-bit identifier). Encoded
as a 32-character uppercase hexadecimal string (no dashes):
`keepsake.vst3.AABBCCDD11223344EEFF5566778899AA`.

VST3 FUIDs are globally unique by design; collision resolution is not expected
to be needed.

#### AU v2 IDs

```
keepsake.au.<type><subtype><manufacturer>
```

`<type>`, `<subtype>`, and `<manufacturer>` are the three 4-character codes
that identify an AU v2 component, concatenated without separators:
`keepsake.au.aufxRvrbAppl`.

These triplets are unique per AU component by Apple's design.

#### Format-specific mapping details

VST3 and AU v2 descriptor mapping tables (metadata extraction, feature mapping)
will be added to this contract when those loaders are implemented. The VST2
mapping below is complete and governs g01.001.

### Descriptor shape (VST2)

Each discovered VST2 plugin maps to one `clap_plugin_descriptor_t` with the
following field assignments:

| CLAP field | Source | Rules |
|---|---|---|
| `clap_version` | Compile-time constant | `CLAP_VERSION` from the CLAP SDK |
| `id` | `keepsake.vst2.<uid>` | See namespace rules above |
| `name` | `effGetEffectName` (opcode 45) | If empty or fails, fall back to `effGetProductString` (opcode 48). If both fail, use the filename stem without extension. |
| `vendor` | `effGetVendorString` (opcode 47) | If empty or fails, use `"Unknown"`. |
| `url` | `NULL` | VST2 ABI provides no URL. |
| `manual_url` | `NULL` | VST2 ABI provides no manual URL. |
| `support_url` | `NULL` | VST2 ABI provides no support URL. |
| `version` | `effGetVendorVersion` (opcode 49) | Format as `"MAJOR.MINOR.PATCH"` by decomposing the integer (e.g., 1100 → `"1.1.0"`). If zero or fails, use `"0.0.0"`. |
| `description` | `NULL` | VST2 ABI provides no description field. |
| `features` | Mapped from category + flags | See feature mapping table below. |

### Feature mapping (VST2)

VST2 category (`effGetPlugCategory`, opcode 35) and flags (`AEffect.flags`)
map to CLAP feature strings as follows:

| VST2 source | CLAP features |
|---|---|
| `effFlagsIsSynth` flag set | `"instrument"` |
| `kPlugCategEffect` | `"audio-effect"` |
| `kPlugCategSynth` | `"instrument"`, `"synthesizer"` |
| `kPlugCategAnalysis` | `"audio-effect"`, `"analyzer"` |
| `kPlugCategMastering` | `"audio-effect"`, `"mastering"` |
| `kPlugCategSpacializer` | `"audio-effect"` |
| `kPlugCategRoomFx` | `"audio-effect"`, `"reverb"` |
| `kPlugSurroundFx` | `"audio-effect"`, `"surround"` |
| `kPlugCategRestoration` | `"audio-effect"`, `"restoration"` |
| `kPlugCategGenerator` | `"instrument"` |
| `kPlugCategShell` | See shell plugin rules below |
| `kPlugCategUnknown` or query fails | `"audio-effect"` (safe default) |

The `effFlagsIsSynth` flag takes precedence over category: if the flag is set,
`"instrument"` must appear in the features array regardless of category.

The features array is always null-terminated.

### Shell plugin rules

VST2 shell plugins (`kPlugCategShell`) are containers that expose multiple
sub-plugins via `effShellGetNextPlugin` (opcode 71). Each sub-plugin has its
own `uniqueID` and name. Keepsake must enumerate shell sub-plugins and create
a separate descriptor for each, using the sub-plugin's `uniqueID` for the
plugin ID and its name for the descriptor name. The shell container itself does
not get a descriptor.

Shell plugin support is deferred from g01.001 — the initial proof-of-concept
does not need to handle shells. This section documents the required behavior
for when support is added.

### Factory lifecycle

The factory follows the standard CLAP lifecycle and is format-agnostic — it
serves descriptors from all supported format loaders through a single factory
instance:

1. **Entry init:** The host loads `keepsake.clap` and calls `clap_entry.init(plugin_path)`.
   Keepsake uses `plugin_path` to locate its configuration file and cached scan
   data. Init must not display GUI or perform user interaction. Init must not
   block for a full plugin scan — use cached results when available.

2. **Factory retrieval:** The host calls `clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID)`.
   Keepsake returns its `clap_plugin_factory_t` pointer. This call is
   thread-safe.

3. **Plugin enumeration:** The host calls `get_plugin_count()` and
   `get_plugin_descriptor(index)` for each discovered plugin. These calls must
   be thread-safe and must return immediately from cached data. Descriptors are
   owned by the factory and must remain valid until `deinit()`. The host does
   not free them.

4. **Plugin instantiation:** The host calls `create_plugin(factory, host, plugin_id)`
   with a `keepsake.<format>.*` ID. Keepsake identifies the format from the ID,
   creates the plugin instance, and internally sets up the subprocess bridge
   for the appropriate loader. Returns `NULL` if the plugin ID is unknown or
   the source binary cannot be located. The host owns the returned
   `clap_plugin_t` and frees it via `plugin->destroy()`.

5. **Entry deinit:** The host calls `clap_entry.deinit()`. Keepsake frees all
   cached descriptors, closes any open scan state, and cleans up.

### Threading rules

| Operation | Thread safety |
|---|---|
| `clap_entry.init()` / `deinit()` | Single-threaded (not called concurrently) |
| `clap_entry.get_factory()` | Thread-safe |
| `factory->get_plugin_count()` | Thread-safe |
| `factory->get_plugin_descriptor()` | Thread-safe |
| `factory->create_plugin()` | Thread-safe |

The factory must be safe for hosts that scan from multiple threads (some hosts
do this).

### Scan and cache interaction

The factory exposes whatever the most recent completed scan produced, across
all supported formats. Scan details (paths per format, caching strategy, rescan
triggers) are outside this contract's scope — they will be governed by the
platform config contract (005) once authored. For g01.001, a hardcoded scan
path or a single configured path is sufficient.

### Stability guarantees

- A plugin ID (`keepsake.<format>.<uid>`) must remain stable across scans and
  Keepsake updates for the same plugin binary with the same native identifier.
  Hosts rely on plugin ID stability for session recall.
- Descriptor field values (name, vendor, version) may change if the source
  plugin itself is updated, but the ID must not change.
- Descriptor pointer addresses may change across `init()`/`deinit()` cycles,
  but must be stable within a single init session.

## Validation

- Verify that a VST2 plugin loaded via the factory produces a descriptor with
  a correctly formed `keepsake.vst2.<uid>` ID matching its `AEffect.uniqueID`.
- Verify that name, vendor, and version fields are populated from the source
  plugin's metadata.
- Verify that features contain the correct category mapping.
- Verify that the factory returns consistent results when called from multiple
  threads concurrently.
- Verify that descriptors remain valid pointers from `init()` through `deinit()`.
- When VST3 and AU v2 loaders are implemented: verify their ID formats and
  descriptor mappings follow the rules defined in this contract.

## Roadmap Impact

- g01.001: CLAP factory proof-of-concept — first implementation of this contract
- All subsequent milestones that add descriptor fields, shell plugin support,
  or modify factory behavior

## Planning Notes

This contract defines the stable public interface between Keepsake and CLAP
hosts. Changes to this contract affect every host that has scanned Keepsake
plugins, so the ID namespace and stability rules are designed to be durable
from the start.

The descriptor shape intentionally maps only what each source format's ABI
provides. Fields that a format cannot populate (`url`, `manual_url`,
`support_url`, `description`) are `NULL` rather than fabricated.

The contract is structured so that VST3 and AU v2 descriptor mapping tables
can be added as sections alongside the existing VST2 tables when those loaders
are implemented, without changing the factory lifecycle or stability rules.

## Next Task

Execute g01.001 (VST2 first). Author the VeSTige loader ABI contract (003) if
the loader boundary needs stabilizing, or proceed directly since this contract
defines the factory interface. VST3 and AU v2 descriptor mapping tables will be
added to this contract when those loaders are implemented.
