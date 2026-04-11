# G01.011 — Editor Header Bar

Status: complete
Owner: Infinite Loop Audio
Updated: 2026-04-11
Governing refs:
  - docs/contracts/006-process-isolation-policy.md
Auto-continuation: allowed within g01

## Scope

Add a thin toolbar to the top of every floating editor window showing plugin
info and quick-access controls. This is the first real UX surface Keepsake
owns — everything else is invisible bridging.

## Steps

### 1. Header bar UI (macOS first)

Add a native toolbar to the floating NSWindow in `bridge_gui_mac.mm`:

- Plugin name + format badge (VST2 / VST3 / AU)
- Architecture indicator (native / Rosetta / 32-bit)
- Isolation mode indicator + toggle (shared → per-binary → per-instance)
- Bridge process status (connected / crashed)
- "Open Soundcheck" button (launches companion app, deferred until
  Soundcheck exists — placeholder for now)

The toolbar sits above the VST2/VST3 editor content view. The editor
window height increases by the toolbar height.

### 2. Isolation toggle

Clicking the isolation indicator cycles through modes for this plugin.
The change is written back to config.toml (or Soundcheck API when
available). Takes effect on next instantiation, not mid-session.

### 3. Platform stubs

Create stub header bars for Windows (HWND toolbar) and Linux (GTK/X11
toolbar) that display the same information. Implementation deferred to
g01.012.

## Evidence Requirements

- Screenshot of a VST2 editor with the Keepsake header bar visible
- Isolation mode readable from the toolbar
- Format and architecture badges correct
