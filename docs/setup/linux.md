# Setting up Keepsake on Linux

> **Experimental:** Linux support is available in v0.1-alpha but is still being validated. Basic scan, load, and UI have been confirmed in REAPER on Ubuntu ARM64, but coverage with real-world plugins and the x86_64 artifact target is still being established. Treat this as an early preview — feedback welcome.

This guide walks through installing Keepsake on Linux (x86_64), from downloading the release to getting your legacy plugins appearing in a CLAP host.

---

## What you need

- Linux (x86_64) — Ubuntu, Fedora, Arch, or similar
- A CLAP-capable host — [REAPER](https://www.reaper.fm/download.php) is the recommended option for testing this alpha
- At least one VST2 plugin you want to bridge (`.so` files, typically in `~/.vst/` or `/usr/lib/vst/`)

**32-bit plugin note:** 32-bit VST2 bridging on Linux requires multilib support (32-bit libraries installed). This is architecturally supported but has not been validated in this alpha — treat it as experimental.

---

## Step 1 — Download Keepsake

Go to the [Keepsake releases page](https://github.com/infinite-loop-audio/keepsake/releases/tag/v0.1-alpha) and download:

```
keepsake-linux-x64-v0.1-alpha.tar.gz
```

---

## Step 2 — Unpack the archive

```sh
tar xzf keepsake-linux-x64-v0.1-alpha.tar.gz
```

You'll find two files:

```
keepsake.clap
keepsake-bridge
```

Both files are required. **They must stay in the same directory.** Keepsake uses `keepsake-bridge` to run each legacy plugin in its own isolated process.

---

## Step 3 — Find or create the CLAP plugin directory

CLAP hosts on Linux look in these standard locations:

| Path | Notes |
|---|---|
| `~/.clap/` | Per-user (recommended for most setups) |
| `/usr/lib/clap/` | System-wide (requires root) |

Create the per-user directory if it doesn't exist:

```sh
mkdir -p ~/.clap
```

---

## Step 4 — Install Keepsake

Copy both files to your CLAP directory:

```sh
cp keepsake.clap keepsake-bridge ~/.clap/
```

Or system-wide (requires sudo):

```sh
sudo cp keepsake.clap keepsake-bridge /usr/lib/clap/
```

> **Important:** Both `keepsake.clap` and `keepsake-bridge` must be in the same directory. Keepsake looks for the bridge binary alongside itself — if it's missing, plugins will fail to load.

Make sure `keepsake-bridge` is executable:

```sh
chmod +x ~/.clap/keepsake-bridge
```

---

## Step 5 — Rescan plugins in your host

Open your CLAP host and trigger a rescan. In REAPER:

1. Go to **Options → Preferences → Plug-ins → CLAP**
2. Click **Re-scan** or **Clear cache and re-scan**

After the scan, your VST2 plugins should appear in the CLAP section of the plugin browser — each with their original name and vendor.

---

## Step 6 — Verify it's working

Find one of your VST2 plugins in the CLAP browser and load it. If it loads and passes audio, Keepsake is working. Plugin UI (editor window) support on Linux is still in development — you may or may not see a GUI depending on the plugin and host.

---

## Where Keepsake looks for VST2 plugins

By default, Keepsake scans these locations:

| Location | Notes |
|---|---|
| `~/.vst/` | Per-user VST2 plugins |
| `/usr/lib/vst/` | System-wide VST2 plugins |
| `/usr/local/lib/vst/` | Locally installed VST2 plugins |

If your plugins are elsewhere — a different directory or a custom DAW path — add them via config.

---

## Configuring custom scan paths

Create a config file at:

```
~/.config/keepsake/config.toml
```

```sh
mkdir -p ~/.config/keepsake
# Then create/edit ~/.config/keepsake/config.toml
```

Add extra scan paths:

```toml
[scan]
vst2_paths = ["/home/yourname/plugins", "/opt/vstplugins"]
```

To replace the default paths entirely:

```toml
[scan]
replace_default_vst2_paths = true
vst2_paths = ["/home/yourname/plugins"]
```

If you prefer to set paths via environment variable instead:

```sh
export KEEPSAKE_VST2_PATH="/path/one:/path/two"
```

This overrides both config and defaults (colon-separated paths).

After saving config or setting the env var, rescan in your host. See the [full config reference](config-reference.md) for all available options.

---

## Optional: XDG_CONFIG_HOME

If you use a non-standard XDG config directory, Keepsake respects `XDG_CONFIG_HOME`:

```
$XDG_CONFIG_HOME/keepsake/config.toml
```

---

## Updating Keepsake

To replace an older version:

1. Quit your host
2. Copy the new `keepsake.clap` and `keepsake-bridge` over the existing files
3. Reopen your host and rescan

---

## Something not working?

See the [troubleshooting guide](troubleshooting.md). If you hit a Linux-specific issue that isn't covered there, please open an issue on GitHub — Linux testing is still early and real-world reports are genuinely useful.
