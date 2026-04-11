# G01.009 — 32-Bit Bridge Binaries

Status: complete
Owner: Infinite Loop Audio
Updated: 2026-04-10
Governing refs:
  - docs/contracts/004-ipc-bridge-protocol.md
  - docs/architecture/system-architecture.md
  - docs/vision/001-keepsake-vision.md
Auto-continuation: allowed within g01

## Scope

Build 32-bit versions of the bridge binary (`keepsake-bridge-32`) for Windows
and Linux, enabling Keepsake to load 32-bit VST2 (and eventually VST3)
plugins from a 64-bit host. This completes the bitness bridging capability
described in the architecture and vision documents.

macOS 10.15+ dropped 32-bit support entirely — 32-bit bridging is Windows
and Linux only.

## Steps

### 1. 32-bit build toolchain setup

Configure CMake to cross-compile a 32-bit bridge binary:

- **Windows**: build `keepsake-bridge-32.exe` targeting x86 (either via
  MSVC cross-compilation or MinGW-w64 i686)
- **Linux**: build `keepsake-bridge-32` targeting i386 (requires multilib
  gcc or a 32-bit sysroot)
- CMake toolchain files for each platform

### 2. IPC compatibility

Verify the IPC protocol works across 32/64-bit process boundaries:

- Struct sizes and alignment: `IpcHeader`, `IpcPluginInfo`, and all payload
  structs must have consistent layout between 32-bit and 64-bit builds
- Shared memory pointer math: `sizeof(float)` is 4 on both, but pointer
  sizes differ — verify no pointer-dependent calculations in shared memory
  layout
- Fix any `intptr_t` / `size_t` issues in the protocol

### 3. Architecture detection

Extend the scanner to detect 32-bit plugins:

- Windows: PE header inspection for IMAGE_FILE_MACHINE_I386 vs AMD64
- Linux: ELF header inspection for EM_386 vs EM_X86_64
- Route 32-bit plugins to `keepsake-bridge-32` at scan and instantiation time

### 4. Bundle/package layout

Install the 32-bit bridge binary alongside the 64-bit one:

- macOS: N/A (no 32-bit support)
- Windows: `keepsake-bridge-32.exe` alongside `keepsake.clap`
- Linux: `keepsake-bridge-32` alongside `keepsake.clap`

### 5. Evidence

- A 32-bit VST2 plugin loaded and processed through the 32-bit bridge on
  Windows or Linux
- IPC protocol verified across the 32/64 boundary

## Stop Conditions

- Stop if multilib toolchain setup is impractical for CI (document manual
  build instructions instead)
- Stop if struct alignment differs between 32/64 in ways that require
  protocol changes

## Evidence Requirements

- 32-bit plugin producing audio through the bridge on at least one platform
- No IPC corruption across the 32/64 boundary

## Next Task

After this milestone: assess g01 for generation close or additional polish.
