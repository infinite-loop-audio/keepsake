# Building from source

This guide covers building Keepsake locally on all three supported platforms. The CI workflows are the authoritative reference for what's proven — this guide mirrors them closely.

---

## Prerequisites

| Platform | Required |
|---|---|
| All | CMake 3.24+, git |
| macOS | Xcode command line tools (`xcode-select --install`) |
| Linux | GCC or Clang, `build-essential`, `libx11-dev` |
| Windows | Visual Studio 2022 Build Tools (with C++ workload), or MSVC via full VS install |

External dependencies (CLAP SDK 1.2.2, VST3 pluginterfaces v3.8.0) are fetched automatically by CMake via `FetchContent` on first configure. No manual download needed.

VeSTige is already in `vendor/` — do not replace it with the Steinberg VST2 SDK.

---

## macOS

### Install prerequisites

```sh
xcode-select --install
```

CMake 3.24+ via Homebrew if not already present:

```sh
brew install cmake
```

### Build

```sh
cmake --preset default
cmake --build build
```

### Outputs

```
build/keepsake.clap/                          ← plugin bundle
build/keepsake.clap/Contents/MacOS/keepsake   ← main binary
build/keepsake.clap/Contents/Helpers/keepsake-bridge          ← native bridge
build/keepsake.clap/Contents/Helpers/keepsake-bridge-x86_64   ← Rosetta bridge (arm64 host only)
build/clap-scan                               ← CLAP factory inspector
build/bridge-test                             ← bridge integration test
build/test-plugin.vst                         ← test fixture plugin
```

### Quick verification

```sh
# Check the CLAP entry symbol is exported
nm -gU build/keepsake.clap/Contents/MacOS/keepsake | grep _clap_entry

# Run the bridge integration test against the test fixture
build/bridge-test \
  build/keepsake.clap/Contents/Helpers/keepsake-bridge \
  build/test-plugin.vst

# Run the CLAP factory scan against the test fixture
KEEPSAKE_VST2_PATH=build build/clap-scan build/keepsake.clap
```

### Release build

```sh
cmake --preset release
cmake --build build-release
```

Output goes to `build-release/keepsake.clap`.

---

## Linux

### Install prerequisites

On Ubuntu / Debian:

```sh
sudo apt-get update
sudo apt-get install -y cmake build-essential libx11-dev
```

On Fedora / RHEL:

```sh
sudo dnf install -y cmake gcc-c++ libX11-devel
```

On Arch:

```sh
sudo pacman -S cmake gcc libx11
```

`libx11-dev` / `libX11-devel` is required because the bridge links against X11 when available (for future GUI work). The build does not fail without it but will emit a warning and skip the X11 link.

### Build

The `default` preset works on Linux the same as macOS:

```sh
cmake --preset default
cmake --build build
```

Or equivalently without the preset (what CI uses):

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Outputs

```
build/keepsake.clap      ← plugin binary
build/keepsake-bridge    ← bridge binary (must stay adjacent to keepsake.clap)
build/clap-scan          ← CLAP factory inspector
build/bridge-test        ← bridge integration test
build/keepsake-scan      ← plugin path scanner
build/host-test          ← basic host lifecycle test
build/host-test-threaded ← threaded host lifecycle test
build/test-plugin.so     ← test fixture plugin
```

### Quick verification

```sh
# Run the bridge integration test
build/bridge-test build/keepsake-bridge build/test-plugin.so

# Run the CLAP factory scan
KEEPSAKE_VST2_PATH=build build/clap-scan build/keepsake.clap
```

Expected output from the scan includes:

```
Keepsake Test Plugin
keepsake.vst2.4B505354
```

### Release build

```sh
cmake --preset release
cmake --build build-release --config Release -j$(nproc)
```

Outputs: `build-release/keepsake.clap`, `build-release/keepsake-bridge`.

### 32-bit bridge (optional)

For testing 32-bit VST2 bridging on Linux, you need multilib support:

```sh
sudo apt-get install -y gcc-multilib g++-multilib
```

Then build with the 32-bit preset:

```sh
cmake --preset linux-x86
cmake --build build-linux-x86
```

This produces `build-linux-x86/keepsake-bridge` compiled as a 32-bit binary. It isn't a full plugin build — it's the bridge helper only, used by the 64-bit plugin to host 32-bit VST2 instances.

---

## Windows

### Install prerequisites

Install [Visual Studio 2022 Build Tools](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022) with the **Desktop development with C++** workload. This provides MSVC, the Windows SDK, and CMake integration.

CMake 3.24+ may be bundled with VS; verify with `cmake --version`. If not, install it from [cmake.org](https://cmake.org/download/) or via `winget`:

```powershell
winget install Kitware.CMake
```

### Build

Open a **Developer Command Prompt for VS 2022** (or Developer PowerShell), then:

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

> **Why not `--preset default`?** The `default` preset doesn't specify a generator or architecture, so CMake picks whatever is available. On Windows this usually resolves to Visual Studio with x64, which is correct — but if you have multiple VS versions installed, be explicit: `cmake -B build -G "Visual Studio 17 2022" -A x64`.

### Outputs

Visual Studio builds put binaries in a config subdirectory:

```
build/Debug/keepsake.clap        ← plugin binary
build/Debug/keepsake-bridge.exe  ← bridge binary (must stay adjacent to keepsake.clap)
build/Debug/clap-scan.exe        ← CLAP factory inspector
build/Debug/windows-vst2-probe.exe
build/Debug/windows-clap-host.exe
build/Debug/host-probe.clap      ← CLAP host probe tool
build/Debug/test-plugin.dll      ← test fixture plugin
```

### Quick verification

There is no `bridge-test` binary on Windows (the tool uses Unix-specific IPC). Basic output verification:

```powershell
# Check outputs exist
Test-Path build/Debug/keepsake.clap
Test-Path build/Debug/keepsake-bridge.exe
Test-Path build/Debug/test-plugin.dll
```

For real-host validation, install the built binaries to a CLAP path and rescan in REAPER (see the [Windows setup guide](windows.md)).

### Release build

```powershell
cmake --preset release
cmake --build build-release --config Release
```

Outputs: `build-release/Release/keepsake.clap`, `build-release/Release/keepsake-bridge.exe`.

### 32-bit bridge (optional)

The `windows-x86` preset builds the 32-bit bridge helper. It requires Visual Studio 2022 to be installed (uses the VS generator).

```powershell
cmake --preset windows-x86
cmake --build build-win32 --config Debug
```

This produces `keepsake-bridge-32.exe` (named explicitly to distinguish it from the 64-bit bridge). The 64-bit host process uses this binary when it detects a 32-bit VST2 plugin.

---

## Effigy tasks

Once built, use Effigy for validation rather than running tools manually:

```sh
effigy doctor                   # verify the build environment is healthy
effigy qa                       # full quality check
effigy demo list                # list available demo proofs
effigy demo:supported-proof     # run the primary validation suite (macOS + REAPER + VST2)
effigy demo run <id>            # run a specific demo
```

The `doctor` task catches common configuration problems (missing bridge binary, wrong output paths, etc.) and is worth running after any significant build change.

---

## Parallel builds

Add `-j` to speed up compilation:

```sh
# macOS / Linux
cmake --build build -j$(nproc)

# Windows (PowerShell)
cmake --build build --config Debug -- /m
```

---

## Troubleshooting builds

**CMake can't find a compiler (Windows)**
Open the Developer Command Prompt for VS 2022 rather than a plain terminal. CMake needs the MSVC environment variables to be set.

**FetchContent fails (no network)**
CMake downloads CLAP SDK and VST3 pluginterfaces on first configure. If your environment has no network access, mirror these repos and point CMake at them via `FETCHCONTENT_SOURCE_DIR_CLAP` and `FETCHCONTENT_SOURCE_DIR_VST3_PLUGINTERFACES` cache variables.

**Linux: `libx11-dev` not found**
The build will still succeed — X11 is optional. You'll see a CMake message that X11 was not found; the bridge compiles without it. Install `libx11-dev` to avoid this.

**macOS: `keepsake-bridge-x86_64` not built**
This target is only built when the host is `arm64`. On an Intel Mac you'll get `keepsake-bridge` only (native x86_64). That's correct — an Intel Mac doesn't need the Rosetta bridge.

**Windows: output at `build/keepsake.clap` vs `build/Debug/keepsake.clap`**
Visual Studio generators put binaries in a config subdirectory (`Debug/`, `Release/`). Single-config generators (Ninja, Unix Makefiles) put them directly in the build root. If you use Ninja on Windows (`-G Ninja`), outputs land at `build/keepsake.clap` directly.
