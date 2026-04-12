# G01.003 — Cross-Platform and Cross-Architecture Support

Status: complete
Owner: Infinite Loop Audio
Updated: 2026-04-10
Governing refs:
  - docs/contracts/001-working-rules.md
  - docs/contracts/004-ipc-bridge-protocol.md
  - docs/architecture/system-architecture.md
Auto-continuation: allowed within g01

## Scope

Make the full pipeline (scan → factory → bridge → audio) work on all three
target platforms (macOS, Windows, Linux) and add cross-architecture bridging
on macOS (x86_64 plugins on Apple Silicon via Rosetta 2).

The current code is macOS-first with POSIX internals that partially cover
Linux. Windows process spawning, shared memory, and pipe I/O are not yet
implemented. This milestone closes those gaps before more features are built
on top of platform-specific code.

This milestone does **not** include:
- 32-bit bridge binaries (Windows x86, Linux x86) — deferred; the
  architecture is ready but building and testing 32-bit requires multilib
  toolchains
- Linux arm64 host → x86_64 plugin bridging via userspace translation
  (`FEX`/similar) — deferred to a later milestone; treat as experimental
  future work, not Rosetta-equivalent release scope
- GUI forwarding
- Scan caching or config.toml
- Plugin state save/restore

## Steps

### 1. Platform abstraction layer

Extract platform-specific code into a clean abstraction:

- **Process spawning:** `platform_spawn(binary, &pid, &pipe_to, &pipe_from)`
  - macOS/Linux: fork + exec + pipe + dup2 (existing code)
  - Windows: CreateProcess with redirected stdin/stdout via CreatePipe
- **Process management:** `platform_kill(pid)`, `platform_wait(pid, timeout)`
  - macOS/Linux: kill + waitpid (existing)
  - Windows: TerminateProcess + WaitForSingleObject
- **Shared memory:** `platform_shm_create(name, size)`, `platform_shm_open(name, size)`, `platform_shm_close()`
  - macOS/Linux: shm_open + mmap (existing)
  - Windows: CreateFileMappingA + MapViewOfFile
- **Pipe I/O:** read/write helpers already use raw fd I/O; on Windows, use
  ReadFile/WriteFile on HANDLE, or map HANDLEs to C runtime fds via
  _open_osfhandle
- **Timed pipe read:** poll() on POSIX, WaitForSingleObject on Windows

These abstractions live in `src/platform.h` (header with platform-selected
inline implementations or a small .cpp per platform).

Acceptance:
- All platform-specific code is behind the abstraction
- macOS build still works and passes existing tests

### 2. Windows support

Implement the Windows backends:

- CreateProcess-based subprocess spawning with pipe redirection
- CreateFileMappingA / MapViewOfFile shared memory
- Windows pipe I/O (anonymous pipes via CreatePipe)
- WaitForSingleObject for timed reads and process monitoring
- VST2 plugin loading: LoadLibraryA / GetProcAddress (already stubbed)
- Directory scanning: std::filesystem works on Windows with MSVC
- CMake: ensure the build produces `keepsake.clap` (DLL with .clap extension)
  and `keepsake-bridge.exe`

Acceptance:
- `cmake --preset default && cmake --build --preset default` succeeds on
  Windows with MSVC or MinGW
- bridge-test passes with a real Windows VST2 DLL (if available for testing)

### 3. Linux verification

Verify the existing POSIX code works on Linux:

- VST2 plugins are `.so` files — no bundle resolution needed
- dlopen/dlsym should work as-is
- shm_open may require linking `-lrt` on some distributions
- Scan paths: `~/.vst`, `/usr/lib/vst`, `/usr/local/lib/vst`
- CMake: verify the build produces `keepsake.clap` (.so with .clap extension)
  and `keepsake-bridge`

Acceptance:
- Build succeeds on Linux (gcc or clang)
- bridge-test passes with a Linux VST2 .so (if available)

### 4. macOS cross-architecture bridge

Build `keepsake-bridge` for x86_64 in addition to the native arm64 build:

- CMake target: `keepsake-bridge-x86_64` built with
  `CMAKE_OSX_ARCHITECTURES=x86_64`
- Installed in the `.clap` bundle at
  `Contents/Helpers/keepsake-bridge-x86_64`
- The factory's architecture detection (from g01.001) already identifies
  x86_64-only plugins — extend `create_plugin` to use the x86_64 bridge
  binary for those plugins
- The x86_64 bridge runs under Rosetta 2 automatically

Acceptance:
- An x86_64-only VST2 plugin (e.g., Serum, Massive) loads and processes
  audio through the x86_64 bridge on an Apple Silicon Mac
- bridge-test passes with an x86_64 plugin using the x86_64 bridge binary

### 5. Evidence collection

- Build logs for all three platforms (or at least macOS + one other)
- bridge-test output with a cross-architecture plugin on macOS
- Record in `docs/logs/`

## Stop Conditions

- Stop if Windows pipe/process model requires fundamentally different IPC
  design (unlikely — the contract's pipe model maps cleanly to Windows)
- Stop if Rosetta 2 cannot run the x86_64 bridge binary (would indicate a
  system configuration issue, not a design problem)
- Stop if 32-bit build is needed for the cross-architecture tests to be
  meaningful (defer to a later milestone)

## Evidence Requirements

- Build succeeds on macOS (both architectures) and at least one of
  Windows/Linux
- x86_64 VST2 plugin produces audio through the Rosetta bridge on Apple
  Silicon
- Platform abstraction is clean enough that adding 32-bit bridge binaries
  later is straightforward

## Next Task

After this milestone: g01.004 for scan caching + config.toml, or g01.004
for GUI forwarding, depending on priority.
