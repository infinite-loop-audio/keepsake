---
title: Windows APC embedded-open thread-model evidence
status: active
owner: Codex
updated: 2026-04-14
tags: [windows, reaper, vst2, gui, debugging]
---

## Summary

This batch continued the Windows REAPER APC embedded-editor lane from the
`13-223500-windows-vm-editor-handoff.md` handoff. The work first fixed the
Windows helper wrapper so unattended REAPER smoke runs were usable in the VM,
then restored elevated install/update flow for the shared CLAP location, then
used narrower instrumentation to isolate the exact blocking calls inside the
embedded editor path.

The key result is that the current failure is no longer an unbounded mystery:

- `effEditGetRect` succeeds on the bridge main thread before editor open
- the same `effEditGetRect` call hangs when re-entered on the dedicated GUI
  thread
- caching the rect avoids that seam
- with the cached rect, the embedded path reaches `effEditOpen`
- APC still hangs in `effEditOpen` for embedded hosting even after replacing the
  custom panel with a native child window

This strongly suggests the current design problem is thread/ownership related,
not just a missing log or a simple Win32 style flag.

## Concrete Evidence

- Fixed `tools/windows-run.cmd` so PowerShell smoke arguments actually match
  `tools/reaper-smoke.ps1` parameter names.
- Restored Windows-local build/install iteration by copying the freshly built
  `keepsake.clap` and `keepsake-bridge.exe` into
  `C:\Program Files\Common Files\CLAP\` using a true UAC-elevated copy step.
- Confirmed the VM session itself runs at medium integrity even though the user
  is in Administrators; direct writes into `Program Files` were failing until
  the copy step used `Start-Process -Verb RunAs`.

Instrumented seam progression:

1. First exact embedded dead zone after the GUI-thread refactor:
   - `gui_open_editor_embedded_impl enter`
   - `IsWindow(parent)` succeeds
   - hang occurs inside `loader->get_editor_rect(w, h)`
2. Narrowed VST2 loader seam:
   - `effEditGetRect` succeeds on the non-GUI thread
   - `effEditGetRect` hangs only when called again on the GUI thread
3. Cached the editor rect in `bridge_loader_vst2.cpp`:
   - embedded path then advances past `get_editor_rect`
4. Next exact embedded seam:
   - custom inner panel creation failed with `GetLastError()==87`
   - reusing the wrapper window as editor parent still reached
     `effEditOpen begin`
   - APC then hung in `effEditOpen`
5. Simplified the embedded host shape:
   - direct custom wrapper/panel chain was replaced with a single native
     `"STATIC"` child host window
   - `CreateWindowExA` for that native child succeeded
   - APC still hung in embedded `effEditOpen`

## Safety Outcome

The batch also tightened one host-safety policy:

- when `IPC_OP_EDITOR_SET_PARENT` times out on Windows but the bridge process is
  still alive, Keepsake now marks embedded GUI as failed for that instance and
  refuses to immediately fall back to a second floating `effEditOpen` on the
  same bridge instance

This is deliberately conservative. A hung editor-open inside the bridge should
not cause Keepsake to immediately attempt another risky GUI open path against
the same plugin instance.

## Research Check

Local source review of open-source references supports the interpretation that
Keepsake is likely approaching the Windows GUI thread model incorrectly:

- JUCE’s Windows VST2 host path opens the editor with a real embedded HWND and
  does `effEditGetRect` before and after `effEditOpen` on the host/editor side,
  not by switching the plugin’s GUI dispatch across multiple unrelated threads
  mid-flow.
- yabridge’s editor architecture explicitly creates and owns a dedicated GUI
  hosting surface plus event/timer loop, and then consistently asks the plugin
  to embed into that owned window. It also keeps a wrapper layer for specific
  host/event reasons instead of mixing ownership ad hoc.

The most important conclusion is not "copy yabridge", but "stop guessing
window-style tweaks when the deeper issue may be that GUI dispatch, window
ownership, and plugin-thread expectations are not aligned."

## Current Interpretation

High-confidence interpretation after this batch:

- the GUI-thread refactor changed the symptom shape, but it did not solve the
  underlying Windows VST2 editor-hosting model mismatch
- APC appears sensitive to which thread calls GUI dispatcher functions
- the current design mixes:
  - bridge main thread creation/init/early VST dispatcher traffic
  - dedicated GUI thread embedded-host window ownership
  - worker-thread timeout wrappers around `effEditOpen`
- that split is likely too ad hoc for a plugin that expects a stable GUI thread
  model

Follow-up evidence after the first research-guided implementation attempt:

- routing the initial `EDITOR_GET_RECT` request through the dedicated GUI thread
  did not improve stability
- instead, that change regressed the build into a pre-script REAPER hang during
  scan/load in the isolated smoke harness
- conclusion: the next architecture move cannot be a piecemeal "move one more
  GUI dispatcher call to the GUI thread" change; it needs to be a more coherent
  ownership model change

Standalone probe matrix added after this batch:

- A new Windows-only standalone VST2 probe (`tools/windows-vst2-probe.cpp`)
  proved that both APC and Serum can:
  - load successfully in a tiny VeSTige host
  - return `effEditGetRect`
  - return `effEditOpen` with a simple native child HWND
  - do so when `effEditOpen` is called on the current thread
- The same standalone probe showed worker-thread editor open is unsafe:
  - APC worker-thread `effEditOpen` failed badly enough that the probe needed a
    hard timeout path
  - Serum worker-thread `effEditOpen` also timed out
- This makes the thread-affinity conclusion much stronger: the issue is not
  "Windows VST2 editors are inherently flaky", it is "our host model is calling
  them from the wrong kind of thread/lifecycle arrangement"

Further probe and REAPER validation after that:

- The standalone probe was extended with `--load-thread current|worker` and a
  bounded worker-load timeout so load/init thread affinity could be tested
  separately from editor-open thread affinity.
- Serum tolerates worker-thread load in the standalone probe.
- APC does not appear to tolerate that model cleanly:
  - APC loads and returns `effEditGetRect` when loaded on the current thread
  - APC worker-thread load was bad enough that the probe process no longer gave
    a clean bounded result in this VM lane
- That made the bridge-side `gui_load_plugin()` experiment the best explanation
  for the earlier "REAPER hung before the smoke script started" regression.
- Reverting `gui_load_plugin()` from bridge init restored REAPER smoke progress:
  - REAPER scanned APC
  - inserted the plugin
  - opened the FX UI far enough for the smoke script to log
    `fx-ui-open-finish`
- With that regression removed, the active failure boundary is again the
  embedded editor open itself:
  - `gui_open_editor_embedded_impl` now runs on the GUI owner thread, not a
    worker timeout thread
  - APC still stops at `effEditOpen begin` against the simple native `"STATIC"`
    child host window
  - host-side safety remains intact because Keepsake can still abandon the live
    bridge instance rather than retrying risky GUI work on a poisoned bridge

## Recommended Next Move

Do not keep iterating on random `CreateWindowEx` variants in the current
structure.

Instead, move into a design/research implementation lane with this question:

"What single thread should own Windows VST2 GUI dispatch for an instance, and
how do we keep that thread isolated enough that a hung plugin editor cannot
lock the CLAP host?"

Practical continuation target:

1. Keep the current safety policy that blocks immediate floating fallback after
   embedded timeout on the same bridge instance.
2. Revert any purely diagnostic experiments that regressed initialization.
3. Keep using the standalone probe as the proving ground before changing the
   bridge:
   - current-thread load + current-thread open is the known-good APC/Serum
     baseline
   - worker-thread open is known-bad
   - APC worker-thread load is now a prime suspect too
4. Design a consistent per-instance GUI ownership model:
   - either the bridge process main thread becomes the coherent Windows VST2
     GUI owner for that instance
   - or a stronger "GUI bridge process is disposable" policy is used when the
     owning GUI thread must be allowed to wedge
5. Use JUCE and yabridge source as architectural references, not as copy-paste
   implementations.

Next task: use the standalone probe to prove whether APC specifically requires
"load + `effEditGetRect` + `effEditOpen`" to stay on the process main thread,
then reshape the bridge around that ownership model instead of continuing
secondary GUI-thread experiments.
