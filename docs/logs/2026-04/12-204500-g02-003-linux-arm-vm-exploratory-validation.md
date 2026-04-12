# Linux Exploratory Validation — Ubuntu ARM VM

Status: recorded
Date: 2026-04-12
Owner: Codex
Roadmap: g02.003
Release lane: experimental Linux exploratory host evidence

## Summary

- Set up an Ubuntu 24.04 ARM64 VM under Parallels on Apple Silicon.
- Built Keepsake natively on Linux ARM64 from the shared repo.
- Installed native ARM64 REAPER 7.69 in the VM.
- Installed Keepsake and the repo `test-plugin.so` into Linux user plugin paths.
- Proved real-host scan/add-FX/UI-open behavior in REAPER.
- After enabling ALSA in REAPER, proved short transport play/stop behavior too.
- This lane remains exploratory because it is Ubuntu ARM64 + native ARM64 REAPER,
  not the current `linux-x64` public artifact target.

## Environment

- VM:
  - Ubuntu 24.04 ARM64
- Host session:
  - GNOME / Wayland
- REAPER:
  - native Linux aarch64 build
  - version 7.69
- repo path in VM:
  - `/media/psf/keepsake`
- build dir in VM:
  - `/home/parallels/work/keepsake-build-arm`

## Build and Tooling Checks

- native Linux ARM build:
  - pass
- bounded harness:
  - `host-test-threaded` pass
- scan tooling:
  - `clap-scan` pass

Commands:

```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug /media/psf/keepsake
cmake --build . -j4 --target keepsake keepsake-bridge host-test-threaded bridge-test clap-scan test-plugin
./host-test-threaded ./keepsake.clap keepsake.vst2.4B505354 --timeout-ms 500 --entry-timeout-ms 5000 --max-memory-mb 1024 --vst-path /tmp/ktest/test-plugin.so
./clap-scan ~/.clap/keepsake.clap
```

Observed:

- native build completed successfully
- `clap-scan` discovered:
  - `keepsake.vst2.4B505354`
  - `Keepsake Test Plugin`
- targeted threaded host test passed end-to-end

## Install Shape

Installed in the VM:

- `~/.clap/keepsake.clap`
- `~/.clap/keepsake-bridge`
- `~/.vst/test-plugin.so`
- `~/.config/keepsake/config.toml`

Config used:

```toml
[scan]
replace_default_vst2_paths = true
vst2_paths = ["/home/parallels/.vst/test-plugin.so"]

[expose]
mode = "all"
```

## REAPER Host Checks

### Real-host UI lane

Result:
- PASS

Checks:
- REAPER discovered `Keepsake Test Plugin`
- FX add succeeded
- UI open succeeded
- UI close succeeded

Key timings:

- scan found: `26 ms`
- add FX finish: `212 ms`
- UI open finish: `274 ms`
- result: `PASS`

### Real-host transport lane

Initial result:
- FAIL

Initial failure:
- `transport-play-timeout`

Follow-up result after enabling ALSA in REAPER:
- PASS

Checks:
- scan/discovery passed
- FX add passed
- UI open passed
- transport play passed
- transport stop passed

Key timings after audio-device fix:

- scan found: `66 ms`
- add FX finish: `403 ms`
- UI open finish: `514 ms`
- transport playing: `2072 ms`
- transport stopped: `3116 ms`
- result: `PASS`

## Release Read

- This is useful Linux host evidence.
- It is still not strong enough to promote Linux into the primary supported `v0.1-alpha` lane.
- Current honest read:
  - Linux exploratory host/runtime proof exists on Ubuntu ARM64 + native ARM64 REAPER + repo `test-plugin.so`
  - the strongest remaining caveat is architecture mismatch with the public `linux-x64` artifact target, not basic host functionality

## Risks / Follow-ups

- This VM is ARM64, not the current `linux-x64` release artifact target.
- The strongest Linux lane would still be real `x86_64` Linux host proof.
- Transport timeout was resolved after enabling ALSA in REAPER.
- Third-party Linux VST2 proof is still missing; this run used the repo test plugin.

## Next Task

Decide whether this Ubuntu ARM64 exploratory host proof is enough to justify shipping Linux artifacts as experimental attachments in `v0.1-alpha`, or whether a real `linux-x64` host pass is still required first.
