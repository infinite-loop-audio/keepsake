# Troubleshooting

Common problems and how to fix them. If something here doesn't resolve your issue, please [open an issue on GitHub](https://github.com/infinite-loop-audio/keepsake/issues) with your platform, host, and what you tried.

---

## Plugin doesn't appear in the host after rescanning

**The most common cause:** Keepsake itself isn't being found by the host.

Check:

1. **Is `keepsake.clap` in the right folder?** Your host only scans specific directories. See the setup guide for your platform:
   - [macOS](macos.md#step-3--create-the-clap-plugin-folder-if-it-doesnt-exist)
   - [Windows](windows.md#step-3--find-or-create-the-clap-plugin-folder)
   - [Linux](linux.md#step-3--find-or-create-the-clap-plugin-directory)

2. **Did you rescan after installing?** Copy the file first, then rescan — not the other way around.

3. **Is the bridge binary present?** On Windows and Linux, `keepsake-bridge.exe` / `keepsake-bridge` must be in the **same folder** as `keepsake.clap`. Without it, Keepsake will fail to initialize.

4. **macOS Gatekeeper blocking it?** See [macOS: Gatekeeper is blocking Keepsake](#macos-gatekeeper-is-blocking-keepsake) below.

---

## Keepsake loads but no legacy plugins appear inside it

Keepsake is being found by the host, but the plugin browser shows Keepsake as empty — no bridged plugins.

Check:

1. **Are your VST2 plugins in the default scan paths?**
   - macOS: `~/Library/Audio/Plug-Ins/VST/` or `/Library/Audio/Plug-Ins/VST/`
   - Windows: `%PROGRAMFILES%\VSTPlugins\`, `%PROGRAMFILES%\Steinberg\VSTPlugins\`, and variants
   - Linux: `~/.vst/`, `/usr/lib/vst/`, `/usr/local/lib/vst/`

   If your plugins are elsewhere, add the path in [config.toml](config-reference.md#vst2_paths).

2. **Check the expose settings.** The default config only exposes bridge-required VST2 (`vst2_bridged = true`). If you have native-arch VST2 plugins (e.g., native arm64 VST2 on Apple Silicon), they're off by default. Enable them:
   ```toml
   [expose]
   vst2_native = true
   ```

3. **Force a rescan.** The scan cache may be stale. Add this to config.toml and reload:
   ```toml
   [scan]
   rescan = true
   ```
   Keepsake will rescan on next host startup and reset this flag automatically.

4. **Check the host console / log output.** Keepsake prints scan results to stderr. In REAPER, open **View → Show REAPER console** before loading Keepsake — you'll see lines like:
   ```
   keepsake: scanning /Library/Audio/Plug-Ins/VST
   keepsake: found plugin: Serum (path=/Library/Audio/Plug-Ins/VST/Serum.vst)
   ```
   If you see no scan lines at all, Keepsake may not be initializing. If you see scan lines but no plugins, check the path.

---

## macOS: Gatekeeper is blocking Keepsake

You see a message like _"keepsake.clap" cannot be opened because the developer cannot be verified_ or _"keepsake.clap" is damaged and can't be opened._

This is macOS blocking the plugin because it isn't signed by an Apple-registered developer. This is expected for this alpha release.

**Fix:**

1. Open **System Settings → Privacy & Security**
2. Scroll to the **Security** section
3. Find the message about `keepsake.clap` being blocked
4. Click **Allow Anyway**
5. Restart your host and rescan

If the Allow Anyway button doesn't appear, try this from Terminal:

```sh
xattr -d com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/keepsake.clap
```

This removes the quarantine flag that Gatekeeper puts on downloaded files.

> If you installed to `/Library/Audio/Plug-Ins/CLAP/` (system-wide), use `sudo` and adjust the path accordingly.

---

## macOS: Plugin editor window doesn't appear

The plugin loads (you can see it in the mixer, parameters respond) but the editor/UI window never opens.

1. **The UI is a separate window.** On macOS in the current alpha, bridged plugin editors open in their own floating window — not embedded inside the host. Look for a window that may be behind your host.

2. **Try clicking the plugin name or edit button** in your host. Some hosts open the editor on demand rather than automatically.

3. **Check mac_mode in config.** If you've set `mac_mode` to anything other than `"live"` or `"auto"`, reset it:
   ```toml
   [gui]
   mac_mode = "live"
   ```

4. **Embedded editors are not yet fully supported.** The current validated UI posture is floating windows. If your host requires embedded editors, this may not work in v0.1-alpha.

---

## macOS: Rosetta plugins not appearing (Apple Silicon)

On an Apple Silicon Mac, Intel (x86_64) VST2 plugins should appear automatically — Keepsake bridges them using Rosetta 2. If they're not appearing:

1. **Verify Rosetta 2 is installed.** Open Terminal and run:
   ```sh
   /usr/bin/arch -x86_64 echo ok
   ```
   If this fails, install Rosetta: `softwareupdate --install-rosetta`

2. **Check that `keepsake-bridge-x86_64` is inside the bundle.** In Finder, right-click `keepsake.clap` → **Show Package Contents**, then navigate to `Contents/Helpers/`. You should see both `keepsake-bridge` and `keepsake-bridge-x86_64`.

3. **Force a rescan** in case the cache is stale (see above).

---

## Windows: Bridge binary missing error / plugins fail to load

You see an error in the host, or plugins appear but won't instantiate.

**Check that `keepsake-bridge.exe` is in the same folder as `keepsake.clap`.** Both files must be co-located. If you only copied `keepsake.clap`, go back and copy `keepsake-bridge.exe` alongside it.

If Windows Defender removed or quarantined `keepsake-bridge.exe`, check Windows Security → Protection History and restore it if found there.

---

## Windows: SmartScreen blocks the bridge executable

When a bridged plugin loads for the first time, Windows SmartScreen may block `keepsake-bridge.exe`:

1. Click **More info** in the SmartScreen dialog
2. Click **Run anyway**

This only needs to happen once. Subsequent loads should proceed without the prompt.

---

## Linux: Bridge not executable

If plugins fail to load on Linux, check that `keepsake-bridge` is executable:

```sh
chmod +x ~/.clap/keepsake-bridge
```

Or if you installed system-wide:

```sh
sudo chmod +x /usr/lib/clap/keepsake-bridge
```

---

## Host shows duplicate plugin entries

If you see the same plugin appearing multiple times in your browser, Keepsake may be scanning the same plugin from multiple paths (e.g., the plugin exists in both `~/Library/Audio/Plug-Ins/VST/` and a custom path you added).

Fix: use `replace_default_vst2_paths = true` to only scan your explicit paths, or remove the duplicate path from your config.

---

## Performance: too many processes

With `isolation = "per-instance"` (the default), each plugin instance runs in its own process. If you're running many instances, this adds up. To reduce overhead:

```toml
[isolation]
default = "per-binary"   # one process per unique plugin binary
```

Or for known-stable plugins:

```toml
[[isolation.override]]
match = "/Library/Audio/Plug-Ins/VST/StablePlugin.vst"
mode = "shared"
```

See the [isolation section in the config reference](config-reference.md#isolation--process-isolation-behaviour) for details.

---

## Scan is slow on first load

On the first load after installation (or after a forced rescan), Keepsake probes each plugin binary to extract metadata. This can take a few seconds with a large plugin collection. Results are cached — subsequent loads are fast.

To force a clean rescan (e.g., after installing new plugins):

```toml
[scan]
rescan = true
```

---

## VST3 or AU plugins not showing up

VST3 and AU support are **experimental in v0.1-alpha**. They are disabled by default. To enable:

```toml
[expose]
vst3 = true   # experimental
au = true     # experimental, macOS only
```

If you enable these and nothing shows up, it may be a scanning or compatibility issue. Please open an issue with details about what plugins you're trying to bridge.

---

## Something else entirely

Check the Keepsake log output in your host's console. Keepsake writes diagnostic messages to stderr — most hosts expose this via a log or console view. The messages follow the format:

```
keepsake: <message>
bridge/vst2: <message>
bridge/vst3: <message>
```

If you're seeing an error there that isn't covered here, please paste it in a [GitHub issue](https://github.com/infinite-loop-audio/keepsake/issues).
