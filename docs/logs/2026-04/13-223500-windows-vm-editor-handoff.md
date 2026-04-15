---
title: Windows VM editor-hosting handoff
status: active
owner: Codex
updated: 2026-04-13
tags: [coordination, handoff]
---

## What This Thread Was Doing

This thread was pushing the Windows REAPER lane from vague “APC locks up” symptoms into a real, instrumented editor-hosting investigation. The work started with manual REAPER checks, then added Windows helper scripts, a REAPER smoke path, build/install hash verification, build stamping in logs, and finally a Windows GUI-thread refactor plus targeted trace points around the embedded editor open path. The current goal is still the same: get APC and Serum loading through Keepsake on Windows in a way that preserves real embedded-host behavior instead of falling back to brittle floating-window hacks.

## Why It Matters

Windows is the weakest major release lane right now. macOS primary evidence is strong, Linux exploratory evidence exists, but Windows still has a real editor-hosting defect in the REAPER lane. Until that is understood and fixed, Windows stays experimental and the alpha release posture stays narrower than the repo’s longer-term goal. This work matters because it is the difference between “Windows artifacts exist” and “Windows actually behaves like a host-integrated bridge.”

## Current State

- Done so far: Windows helper scripts exist and are usable; Windows build/install hash verification is working; build IDs now show in both plugin and bridge logs; the Windows VM repo is current; the embedded editor path now runs on a dedicated GUI thread and emits queue/handoff traces.
- Still open: APC still locks REAPER on Windows during embedded open. The current stamped run proves the GUI-thread handoff itself works, but the embedded path still times out before any of the old inner editor-open logs appear.
- Active spec lane: none; execution is against the existing release and bridge architecture surfaces.
- Canonical refs: `~/Dev/projects/keepsake/README.md`, `~/Dev/projects/keepsake/AGENTS.md`, `~/Dev/projects/keepsake/docs/README.md`, `~/Dev/projects/keepsake/docs/architecture/system-architecture.md`, `~/Dev/projects/keepsake/docs/contracts/001-working-rules.md`, `~/Dev/projects/keepsake/docs/releases/v0.1-alpha.md`, `~/Dev/projects/keepsake/docs/releases/v0.1-alpha-validation-matrix.md`
- Remaining continuation envelope: stay in the Windows REAPER editor-hosting lane only. Do not broaden into general release cleanup or other platform work until the Windows embedded-open defect is either fixed or sharply bounded.
- Lane budget / pause signal: pause here because the next useful step should happen inside the Windows VM Codex app where REAPER, logs, and helper scripts are local and the SSH/control loop is no longer slowing iteration.
- Key files:
  - `~/Dev/projects/keepsake/src/bridge_gui_stub_windows.cpp`
  - `~/Dev/projects/keepsake/src/plugin_gui.cpp`
  - `~/Dev/projects/keepsake/src/bridge_main.cpp`
  - `~/Dev/projects/keepsake/src/bridge_loader_vst2.cpp`
  - `~/Dev/projects/keepsake/src/bridge_loader_vst2_platform.cpp`
  - `~/Dev/projects/keepsake/tools/reaper-smoke.ps1`
  - `~/Dev/projects/keepsake/tools/windows-run.cmd`
  - `~/Dev/projects/keepsake/tools/windows-update-install.cmd`

## Boundaries

- Stay within the Windows REAPER editor-hosting/debugging lane for Keepsake `v0.1-alpha`.
- Do not widen into Linux, macOS, or general release-packaging work from this handoff.
- Do not introduce plugin-name-specific hacks for APC or Serum.
- Do not use or reference the Steinberg VST2 SDK; VST2 stays VeSTige-only.
- Follow repo constraints from [AGENTS.md](~/Dev/projects/keepsake/AGENTS.md).

## Important Context

- Planning lineage: release planning is in the `g02` stream and the Windows work is part of tightening `v0.1-alpha` evidence before publication. The freshest release-facing docs are under `~/Dev/projects/keepsake/docs/releases/` and the active log lane is `~/Dev/projects/keepsake/docs/logs/2026-04/`.
- Spec-to-canonical relationship: this is not a speculative planning lane now. The work is implementation/debugging governed by existing architecture/contracts plus the release docs.
- Decisions and preferences:
  - user wants glue-light communication and minimal filler
  - always leave a `Next task:` line at closeout
  - avoid tiny churny batches; prefer meaningful chunks
  - direct Windows-local work is preferred now because SSH + manual REAPER control was too lossy
  - no per-plugin override tables
  - yabridge is the right architectural reference for editor-hosting shape, but not something to cargo-cult line-for-line
- Open tensions:
  - earlier runs showed APC blocking inside `effEditOpen(parent)`
  - after the GUI-thread refactor, the stamped `9c04249` run changed shape: `gui_call_sync()` posted and the GUI task began, `gui_open_editor_embedded_impl()` entered, then the outer plugin timed out before any later inner logs such as `window-tree phase=before-embed-open` or `effEditOpen begin`
  - the stamped `11a7b5c` run also proved the VM can drift if configure is skipped; this is fixed now by rerunning CMake configure in `tools/windows-update-install.cmd`
  - current useful harness path is now:
    - `tools\windows-run.cmd update-install`
    - `tools\windows-run.cmd smoke-apc-embed`
  - `tools/reaper-smoke.ps1` now copies the Windows debug log into the temp dir, prints a filtered extract, and preserves temp on failure
  - current high-confidence interpretation: the old `effEditOpen()` stall was real, but after the GUI-thread move there is a newer earlier dead zone inside `gui_open_editor_embedded_impl()` before the old `window-tree` / `effEditOpen begin` logs

## Suggested Next Move

Inside the Windows VM Codex app, start by reproducing with the new unattended harness instead of manual REAPER opening:

1. Run `tools\windows-run.cmd update-install`
2. Run `tools\windows-run.cmd smoke-apc-embed`
3. Read the printed `debug_extract` and preserved temp dir
4. Instrument only the next narrow seam inside `gui_open_editor_embedded_impl()`:
   - after `IsWindow(parent)`
   - before/after `loader->get_editor_rect(w, h)`
   - before/after the wrapper `CreateWindowExA`
   - before/after the panel `CreateWindowExA`
5. Re-run `smoke-apc-embed`

That should isolate the first blocking call after GUI-thread handoff. Do not jump back to floating-window policy changes until that exact early seam is identified.

## Completion Protocol

1. Keep the Windows batch visible in the April log lane; add a fresh log entry once the next real seam is isolated or fixed.
2. Keep the release docs honest; do not widen Windows support posture until this lane is materially better.
3. Treat this handoff as a real fresh-thread boundary because the work is moving into a Windows-local Codex session, not because of low context alone.
4. If the next thread gets the first exact blocking call inside `gui_open_editor_embedded_impl()`, log that immediately before attempting another larger refactor.
5. If the next thread fixes the embed dead zone, follow with one Windows APC embedded REAPER validation pass and then update the alpha validation matrix/logs.
6. If the next thread proves embedded hosting is still infeasible for APC under the current subsystem, record that explicitly before revisiting fallback policy.
7. Next task: run `tools\windows-run.cmd smoke-apc-embed` inside the Windows VM Codex app and instrument the first blocking call inside `gui_open_editor_embedded_impl()` after `gui_call_sync task-begin`.
