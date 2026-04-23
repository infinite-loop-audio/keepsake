# Setting up Keepsake on macOS

This guide walks through installing Keepsake on macOS from scratch. It covers downloading the release, placing it in the right folder, and pointing it at your legacy plugins — no prior experience required.

**Current status:** macOS is the primary validated platform for v0.1-alpha. If you're on macOS and using REAPER, this is the most stable path.

---

## What you need

- macOS 11 (Big Sur) or later
- A CLAP-capable host — [REAPER](https://www.reaper.fm/) is the most tested option for this alpha
- At least one VST2 plugin you want to bridge (`.vst` bundles in `~/Library/Audio/Plug-Ins/VST/` or `/Library/Audio/Plug-Ins/VST/`)

**Apple Silicon (M1/M2/M3) note:** Keepsake runs natively on Apple Silicon. If you have Intel (x86_64) VST2 plugins, Keepsake bridges them automatically via Rosetta 2 — you don't need to do anything extra, but Rosetta 2 must be installed (it usually is; if not, macOS will prompt you the first time it's needed).

---

## Step 1 — Download Keepsake

Go to the [Keepsake releases page](https://github.com/infinite-loop-audio/keepsake/releases/tag/v0.1-alpha) and download:

```
keepsake-macos-arm64-v0.1-alpha.zip
```

This file works on both Apple Silicon and Intel Macs.

---

## Step 2 — Unpack the archive

Double-click the `.zip` file in Finder. It will unpack to a single file:

```
keepsake.clap
```

This is a bundle (a folder that looks like a file). Inside it are Keepsake itself and its helper binaries — you don't need to touch them separately.

---

## Step 3 — Create the CLAP plugin folder (if it doesn't exist)

Your host looks for CLAP plugins in a specific folder. On macOS, the standard location is:

```
~/Library/Audio/Plug-Ins/CLAP/
```

The `~/Library` folder is hidden by default in Finder. The easiest way to get there:

1. In Finder, click the **Go** menu
2. Hold **Option** — "Library" appears in the menu
3. Click **Library**
4. Navigate to `Audio → Plug-Ins`
5. If there's no `CLAP` folder, create one (File → New Folder, name it `CLAP`)

Alternatively, open Terminal and run:

```sh
mkdir -p ~/Library/Audio/Plug-Ins/CLAP
```

---

## Step 4 — Install Keepsake

Copy `keepsake.clap` into the CLAP folder you just found or created:

```
~/Library/Audio/Plug-Ins/CLAP/keepsake.clap
```

That's it — no installer, no admin password required for the user-level path.

> **System-wide alternative:** If you want all users on the machine to see Keepsake, use `/Library/Audio/Plug-Ins/CLAP/` instead. This requires an admin password to copy files there.

---

## Step 5 — Dealing with macOS security (Gatekeeper)

macOS will likely block Keepsake the first time because it's not signed by an Apple-identified developer. This is normal for open-source alpha software.

**To allow Keepsake to run:**

1. Open **System Settings** (or System Preferences on older macOS)
2. Go to **Privacy & Security**
3. Scroll down to the **Security** section
4. You'll see a message like "keepsake.clap was blocked because it is not from an identified developer"
5. Click **Allow Anyway**
6. The next time your host loads Keepsake, click **Open** when macOS asks for confirmation

If your host loads Keepsake silently (no prompt appears), try right-clicking `keepsake.clap` in Finder, choosing **Open**, and confirming — this can pre-authorize the bundle.

> This step is a limitation of this alpha release. A signed and notarized release is planned; until then, the manual allow step is required.

---

## Step 6 — Rescan plugins in your host

Open your CLAP host (REAPER is recommended) and trigger a plugin rescan. In REAPER:

1. Go to **Options → Preferences**
2. Click **VST** in the left panel — wait, Keepsake is a CLAP plugin
3. Actually: go to **Options → Preferences → Plug-ins → CLAP**
4. Click **Re-scan** or **Clear cache and re-scan**

After the scan completes, your legacy VST2 plugins will appear in the plugin browser as individual CLAP entries — each with its original name and vendor, as if they were native CLAP plugins.

---

## Step 7 — Verify it's working

Look for your VST2 plugins in the CLAP section of your host's plugin browser. They should appear with their real names (e.g., "Serum", "APC", "Diva") rather than as generic entries.

Load one. If the plugin UI opens in a separate floating window, that's expected — this is the current macOS UI mode. The plugin should respond to parameter changes and pass audio normally.

---

## Where Keepsake looks for VST2 plugins

By default, Keepsake scans these locations:

| Location | Notes |
|---|---|
| `~/Library/Audio/Plug-Ins/VST/` | User VST2 plugins (most common) |
| `/Library/Audio/Plug-Ins/VST/` | System-wide VST2 plugins |

If your plugins are somewhere else — a custom folder, an external drive, a DAW-specific location — see [Configuring custom scan paths](#configuring-custom-scan-paths) below.

---

## Configuring custom scan paths

Create (or edit) a config file at:

```
~/Library/Application Support/Keepsake/config.toml
```

To add extra folders to scan:

```toml
[scan]
vst2_paths = ["/Volumes/SSD/MyPlugins", "/Users/yourname/CustomVSTs"]
```

To completely replace the default scan paths (useful if your plugins are only in a non-standard location):

```toml
[scan]
replace_default_vst2_paths = true
vst2_paths = ["/path/to/my/plugins"]
```

After saving the config, rescan in your host. See the [full config reference](config-reference.md) for all available options.

---

## Exposing VST3 and AU plugins (experimental)

VST3 and AU support exists but is not yet validated for this alpha. If you want to try it:

```toml
[expose]
vst3 = true   # experimental — VST3 bridging
au = true     # experimental — Audio Unit v2 bridging (macOS only)
```

Treat these as early previews. Results will vary.

---

## Updating Keepsake

To replace an older version:

1. Quit your host
2. Delete or overwrite the old `keepsake.clap` in your CLAP folder
3. Copy the new version in its place
4. Reopen your host and rescan

---

## Something not working?

See the [troubleshooting guide](troubleshooting.md) for help with common problems — plugins not appearing, editor issues, Gatekeeper prompts, and more.
