# G01.007 — CI and Cross-Platform Testing

Status: planned
Owner: Infinite Loop Audio
Updated: 2026-04-10
Governing refs:
  - docs/contracts/001-working-rules.md
Auto-continuation: allowed within g01

## Scope

Set up continuous integration to build and test Keepsake on all three target
platforms (macOS, Windows, Linux). The platform abstraction layer (g01.003)
provides the code; this milestone verifies it actually builds and works.

## Steps

### 1. GitHub Actions workflow

Create `.github/workflows/build.yml`:

- **macOS** (arm64 runner): build keepsake.clap + keepsake-bridge +
  keepsake-bridge-x86_64, run bridge-test with a bundled test plugin or
  a minimal VST2 stub
- **Windows** (x64 runner): build keepsake.clap + keepsake-bridge.exe with
  MSVC, verify the build completes
- **Linux** (x64 runner): build keepsake.clap + keepsake-bridge with gcc,
  link against `-lrt`, verify the build completes

### 2. Minimal VST2 test plugin

Create a tiny VST2 plugin (`tools/test-plugin.cpp`) that:

- Exports `VSTPluginMain` returning a valid `AEffect`
- Reports a known `uniqueID`, name, vendor
- Passes audio through unchanged (identity effect)
- Has 1 parameter

This removes the dependency on having real third-party VST2 plugins in CI.

### 3. Automated test suite

Create a test harness that exercises:

- VeSTige loader: load test plugin, verify metadata extraction
- Bridge subprocess: spawn, INIT, ACTIVATE, PROCESS, SHUTDOWN
- Shared memory: round-trip audio data
- Cache: write, read, invalidation
- Factory: scan, descriptor shape, plugin ID format

### 4. Fix platform-specific issues

Address any build or test failures discovered on Windows/Linux:

- Windows: verify CreateProcess pipe redirection, shared memory naming
- Linux: verify shm_open linking, dlopen paths, .so scanning

### 5. Evidence

- Green CI on all three platforms
- Test plugin exercises the full pipeline in CI

## Stop Conditions

- Stop if a platform has fundamental issues that require design changes
  (raise for reassessment rather than hacking around)

## Evidence Requirements

- CI green on macOS, Windows, and Linux
- Test plugin works through the bridge in CI

## Next Task

After this milestone: remaining g01 milestones.
