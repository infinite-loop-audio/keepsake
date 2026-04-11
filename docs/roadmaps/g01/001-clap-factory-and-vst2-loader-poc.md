# G01.001 — CLAP Factory and VeSTige Loader Proof-of-Concept

Status: complete
Owner: Infinite Loop Audio
Updated: 2026-04-10
Governing refs:
  - docs/contracts/001-working-rules.md
  - docs/contracts/002-clap-factory-interface.md
  - docs/architecture/system-architecture.md
  - docs/architecture/product-guardrails.md
Auto-continuation: allowed within g01

## Scope

Stand up the build system, implement a working CLAP plugin factory backed by
VeSTige-based VST2 loading, and prove that a real VST2 plugin appears in a real
CLAP host with correct metadata. This is the foundation milestone — everything
in g01 and beyond depends on the factory and loader working end-to-end.

This milestone does **not** include:
- Out-of-process hosting (subprocess isolation) — deferred to g01.002
- Scan path configuration or caching — hardcoded path is sufficient
- Audio processing bridge — descriptors and metadata only
- GUI / editor forwarding
- Shell plugin enumeration (`kPlugCategShell`)
- IPC bridge protocol

## Steps

### 1. Build system setup

Set up a CMake project that:

- Compiles a single shared library output as `keepsake.clap`
- Pulls the CLAP SDK headers (via git submodule, FetchContent, or vendored copy)
- Includes the VeSTige `aeffectx.h` header (vendored in-repo under an LGPL v2.1
  compatible arrangement)
- Targets macOS (x86_64 + arm64 universal), Windows (x86_64), and Linux (x86_64)
- Produces the correct binary format for each platform:
  - macOS: `.clap` bundle (CFBundlePackageType = BNDL)
  - Windows: `.clap` (renamed `.dll`)
  - Linux: `.clap` (renamed `.so`)
- Sets `CLAP_EXPORT` on the `clap_entry` symbol

Acceptance:
- `cmake --preset default && cmake --build --preset default` succeeds on at
  least one platform
- Output binary exists at the expected path with correct naming

### 2. CLAP entry point and factory stub

Implement the CLAP entry point (`clap_entry`) and a plugin factory that returns
hardcoded test descriptors:

- `clap_entry.init()` — initializes internal state, returns `true`
- `clap_entry.deinit()` — cleans up
- `clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID)` — returns the factory
- `factory->get_plugin_count()` — returns count of discovered plugins
- `factory->get_plugin_descriptor(index)` — returns descriptor pointer
- `factory->create_plugin()` — returns `NULL` for now (audio bridge is out of
  scope)

Start with one or two hardcoded descriptors following the contract 002 shape
to prove the factory compiles and a CLAP host can scan it.

Acceptance:
- The `.clap` binary loads in a real CLAP host (e.g., REAPER with CLAP support,
  Bitwig, or another available host)
- The host's plugin scanner discovers the hardcoded descriptors
- Plugin names appear in the host's plugin browser

### 3. VeSTige loader

Implement a minimal VST2 plugin loader using VeSTige:

- Load a VST2 shared library (`dlopen` / `LoadLibrary`)
- Locate the entry point (`VSTPluginMain` or `main`)
- Call the entry point with a minimal `audioMasterCallback` that handles:
  - `audioMasterVersion` — returns `2400`
  - `audioMasterCurrentId` — returns `0`
  - All other opcodes — returns `0`
- Validate the returned `AEffect*`:
  - Check `magic == kEffectMagic` (0x56737450)
  - Read `uniqueID`, `numInputs`, `numOutputs`, `flags`
- Extract metadata via dispatcher:
  - `effGetEffectName` (opcode 45)
  - `effGetVendorString` (opcode 47)
  - `effGetProductString` (opcode 48)
  - `effGetVendorVersion` (opcode 49)
  - `effGetPlugCategory` (opcode 35)
- Call `effClose` (opcode 1) to clean up
- Do **not** call `effOpen`, `effMainsChanged`, or any processing opcodes —
  this step only extracts metadata for descriptor construction

The loader must never reference or include the Steinberg VST2 SDK. VeSTige only.

Acceptance:
- Given a path to a real VST2 `.vst` / `.dll` / `.so` binary, the loader
  successfully extracts: name, vendor, version, uniqueID, category, and
  input/output channel counts
- The loader correctly handles plugins that return empty strings for name/vendor
  (falls back per contract 002 rules)
- The loader rejects binaries where `magic != kEffectMagic`

### 4. Factory integration

Connect the VeSTige loader to the CLAP factory:

- Replace hardcoded descriptors with descriptors built from real VST2 metadata
- Configure one or more VST2 scan paths (hardcoded or via environment variable
  for this milestone — no config file required yet)
- Scan the configured path(s) at `clap_entry.init()` time
- Build descriptors per contract 002:
  - Plugin ID: `keepsake.vst2.<uid>` from `AEffect.uniqueID`
  - Name, vendor, version: from dispatcher queries with fallback rules
  - Features: mapped from category + flags per contract 002 feature table
- Store descriptors so they remain valid from `init()` through `deinit()`
- `create_plugin()` still returns `NULL` — instantiation requires the audio
  bridge (g01.002+)

Acceptance:
- With one or more real VST2 plugins in the scan path, the CLAP host discovers
  them by name in the plugin browser
- Plugin IDs follow the `keepsake.vst2.<hex>` format
- Features correctly reflect instrument vs. effect classification
- Names and vendors match what the VST2 plugins report

### 5. Evidence collection

Collect and record evidence that the milestone is complete:

- Screenshot or terminal log showing the CLAP host discovering Keepsake-bridged
  plugins
- Log output showing the VeSTige loader extracting metadata from at least one
  real VST2 plugin
- Build log showing successful compilation
- Record the evidence in `docs/logs/`

Acceptance:
- Evidence exists in `docs/logs/` with a clear timestamp and description
- The evidence demonstrates real plugins in a real host, not synthetic or mocked
  data

## Stop Conditions

- Stop if the build cannot produce a valid `.clap` binary on any platform
- Stop if no real VST2 plugin binary is available for testing (the milestone
  cannot close without real evidence)
- Stop if VeSTige loading requires any Steinberg SDK header or reference — this
  is a legal hard stop, raise immediately
- Stop if a CLAP host cannot scan the factory output — investigate before
  proceeding
- Stop if contract 002 cannot be satisfied with the VeSTige ABI (e.g., a
  required descriptor field has no VeSTige source) — raise for contract
  amendment rather than inventing workarounds

## Evidence Requirements

- Plugin appears in a real CLAP host's plugin browser with correct name
- Plugin ID follows `keepsake.vst2.<hex>` format
- Features correctly classify the plugin as instrument or effect
- Build succeeds from a clean checkout on at least one platform
- No Steinberg SDK headers present in the repository

## Next Task

After this milestone closes: author g01.002 (audio processing bridge and
subprocess isolation) or, if the VeSTige loader ABI contract (003) is needed
to stabilize loader behavior, author that contract first.
