# Setting up Keepsake on Windows

> **Experimental:** Windows support is available in v0.1-alpha but has not been validated as thoroughly as macOS. Basic scan, load, and UI have been confirmed in REAPER on Windows 11 (ARM64 VM), but transport and real-world plugin coverage are still being established. Treat this as an early preview — feedback welcome.

This guide walks through installing Keepsake on Windows, from downloading the release to getting your legacy plugins showing up in a CLAP host.

---

## What you need

- Windows 10 or later (x64)
- A CLAP-capable host — [REAPER](https://www.reaper.fm/) is the recommended option for testing this alpha
- At least one VST2 plugin you want to bridge (`.dll` files, typically in `C:\Program Files\VSTPlugins\` or similar)

**32-bit plugin note:** 32-bit VST2 bridging is architecturally supported on Windows via WoW64, but has not been fully validated in this alpha. It may work; treat it as experimental.

---

## Step 1 — Download Keepsake

Go to the [Keepsake releases page](https://github.com/infinite-loop-audio/keepsake/releases/tag/v0.1-alpha) and download:

```
keepsake-windows-x64-v0.1-alpha.zip
```

---

## Step 2 — Unpack the archive

Right-click the `.zip` file and choose **Extract All**, then extract to a temporary folder. You'll find two files:

```
keepsake.clap
keepsake-bridge.exe
```

Both files are required. **They must stay in the same folder.** Keepsake uses `keepsake-bridge.exe` to run each legacy plugin in its own isolated process.

---

## Step 3 — Find or create the CLAP plugin folder

CLAP hosts on Windows look in a few standard locations. The most common are:

| Path | Notes |
|---|---|
| `C:\Program Files\Common Files\CLAP\` | System-wide (requires admin) |
| `%LOCALAPPDATA%\Programs\Common\CLAP\` | Per-user (no admin needed) |

To use the per-user path without needing admin rights:

1. Press **Win + R**, type `%LOCALAPPDATA%`, press Enter
2. Navigate to `Programs → Common`
3. If there's no `CLAP` folder, create one

Or in PowerShell:

```powershell
New-Item -ItemType Directory -Force "$env:LOCALAPPDATA\Programs\Common\CLAP"
```

---

## Step 4 — Install Keepsake

Copy **both** files from the archive into the CLAP folder:

```
C:\Program Files\Common Files\CLAP\keepsake.clap
C:\Program Files\Common Files\CLAP\keepsake-bridge.exe
```

Or into the per-user location:

```
%LOCALAPPDATA%\Programs\Common\CLAP\keepsake.clap
%LOCALAPPDATA%\Programs\Common\CLAP\keepsake-bridge.exe
```

> **Important:** Do not separate the two files. `keepsake.clap` expects to find `keepsake-bridge.exe` in the same directory. If the bridge is missing, plugins will fail to load.

---

## Step 5 — Windows security prompt

Windows Defender SmartScreen may block `keepsake-bridge.exe` the first time it runs, since it isn't signed. If you see a warning:

1. Click **More info**
2. Click **Run anyway**

This is a limitation of this alpha release — signing is planned for a future release.

You may also see a Windows Firewall prompt the first time a bridged plugin loads (the bridge process communicates over a local IPC channel). Allow local network access if prompted.

---

## Step 6 — Rescan plugins in your host

Open your CLAP host (REAPER recommended) and rescan. In REAPER:

1. Go to **Options → Preferences → Plug-ins → CLAP**
2. Click **Re-scan** or **Clear cache and re-scan**

After the scan, your VST2 plugins will appear in the CLAP section of the plugin browser — each with their original name and vendor.

---

## Step 7 — Verify it's working

Find one of your VST2 plugins in the CLAP browser and load it. If it loads and passes audio, Keepsake is working. Plugin UI (editor window) support on Windows is still being developed — you may or may not see a GUI depending on the plugin.

---

## Where Keepsake looks for VST2 plugins

By default, Keepsake scans these locations:

| Location | Notes |
|---|---|
| `%PROGRAMFILES%\VST2\` | Standard 64-bit VST2 path |
| `%PROGRAMFILES%\VSTPlugins\` | Common alternative |
| `%PROGRAMFILES%\Steinberg\VSTPlugins\` | Steinberg default |
| `%PROGRAMFILES(X86)%\VST2\` | 32-bit program path variants |
| `%PROGRAMFILES(X86)%\VSTPlugins\` | |
| `%PROGRAMFILES(X86)%\Steinberg\VSTPlugins\` | |
| `%COMMONPROGRAMFILES%\VST2\` | Common files variants |
| `%COMMONPROGRAMFILES%\VSTPlugins\` | |
| `%COMMONPROGRAMFILES%\Steinberg\VSTPlugins\` | |

If your plugins are in a different location, add them via config — see below.

---

## Configuring custom scan paths

Create a config file at:

```
%APPDATA%\Keepsake\config.toml
```

To open that folder: press **Win + R**, type `%APPDATA%`, press Enter, then navigate to or create a `Keepsake` folder.

Add extra scan paths:

```toml
[scan]
vst2_paths = ["D:\\MyPlugins", "C:\\Custom\\VSTFolder"]
```

To replace the default paths entirely:

```toml
[scan]
replace_default_vst2_paths = true
vst2_paths = ["D:\\MyPlugins"]
```

After saving, rescan in your host. See the [full config reference](config-reference.md) for all available options.

---

## Updating Keepsake

To replace an older version:

1. Quit your host
2. Delete or overwrite both `keepsake.clap` and `keepsake-bridge.exe` in your CLAP folder
3. Copy the new versions in their place
4. Reopen your host and rescan

---

## Something not working?

See the [troubleshooting guide](troubleshooting.md). If you hit a Windows-specific issue that isn't covered there, please open an issue on GitHub — Windows testing is still early and real-world reports are valuable.
