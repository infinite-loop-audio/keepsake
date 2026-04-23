# Setup guides

Everything you need to install Keepsake and get your legacy plugins running in a CLAP host.

---

## Pick your platform

| Platform | Status | Guide |
|---|---|---|
| **macOS** | Primary validated lane | [Setting up on macOS](macos.md) |
| **Windows** | Experimental | [Setting up on Windows](windows.md) |
| **Linux** | Experimental | [Setting up on Linux](linux.md) |

**Start here if you're new:** the [macOS guide](macos.md) is the most thoroughly tested path and the best reference even if you're on another platform.

---

## Reference

- [Config reference](config-reference.md) — all `config.toml` keys, defaults, and examples
- [Troubleshooting](troubleshooting.md) — common problems and fixes
- [Building from source](building.md) — developer build guide for macOS, Windows, and Linux

---

## What "experimental" means

macOS + REAPER + VST2 is the primary validated lane for v0.1-alpha. Windows and Linux have working code and have been lightly tested, but haven't been validated to the same depth. The guides cover both tracks honestly — experimental doesn't mean broken, it means we're still gathering evidence.

See [`known-issues-v0.1-alpha.md`](../known-issues-v0.1-alpha.md) for the full alpha support posture.
