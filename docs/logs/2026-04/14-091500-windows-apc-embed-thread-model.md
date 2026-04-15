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

More granular standalone probe results after extending the probe again:

- The probe now supports independently selecting
  - `--load-thread`
  - `--rect-thread`
  - `--open-thread`
- APC matrix results from that probe:
  - load=current, rect=current, open=current: succeeds
  - load=current, rect=worker, open=current: succeeds
  - load=current, rect=current, open=worker: hangs badly enough to require an
    external kill in this VM lane
  - load=worker, rect=current: also hangs badly enough to require an external
    kill in this VM lane
- The important interpretation is narrower and more useful than before:
  - APC does not appear to require `effEditGetRect` to stay on the main thread
  - APC does appear highly sensitive to moving either plugin load/init or
    `effEditOpen` off the process main thread

First bridge-architecture experiment after that probe evidence:

- Windows GUI ownership in `bridge_gui_stub_windows.cpp` was changed so the
  bridge main thread becomes the Win32 GUI owner instead of spinning up a
  separate dedicated GUI thread.
- This is architecturally closer to the standalone probe's known-good APC path,
  because the bridge process main thread already owns IPC and GUI while audio
  work happens on separate instance worker threads.
- Build/test status from this first pass is mixed:
  - the code builds successfully
  - default-install validation was temporarily obscured by a stale locked
    `Program Files\Common Files\CLAP\keepsake-bridge.exe`
  - isolated-config REAPER smoke against a staged local CLAP path still hung
    before the startup script, so this change is not yet validated as an
    improvement in REAPER
- So the architectural direction is still plausible, but not proven yet in the
  full REAPER lane

Fresh validation after updating the installed `keepsake.clap` and forcing
`KEEPSAKE_BRIDGE_PATH` to the freshly built workspace bridge:

- REAPER did run the new bridge build path:
  - the preserved debug log contains
    - `bridge: gui owner thread init ...`
    - `bridge: gui owner thread ready ...`
- The APC embedded-open failure boundary moved earlier than before:
  - under the older "dedicated GUI helper thread" path, Keepsake got past
    `CreateWindowExA` and then APC hung inside `effEditOpen`
  - under the new "bridge main thread owns Win32 GUI" path, Keepsake gets to
    `gui_open_editor_embedded_impl`, validates the host parent, and gets a
    cached `effEditGetRect`, but then stops at
    `before CreateWindowExA host-child ...`
  - the `after CreateWindowExA host-child ...` line never appears
- Interpretation:
  - "main thread owns all GUI work" is not a complete fix by itself
  - there is likely a second constraint around how/where the embedded host
    child window is created when targeting REAPER's parent HWND
  - that means the likely end state is not "move everything to the main thread",
    but rather "find the exact ownership split that matches a working host
    model" and prove it incrementally

Follow-up hybrid experiment after that:

- A helper window thread was reintroduced only for embedded host-child window
  creation and timer/message pumping.
- APC-facing calls (`effEditGetRect`, `effEditOpen`) still stayed on the bridge
  main thread.
- Result:
  - the helper window thread came up successfully
  - `window_call_sync` reached `task-begin` on that helper thread
  - the log still stopped before `after CreateWindowExA host-child ...`
- Interpretation:
  - the problem is not merely "which thread later calls APC `effEditOpen`"
  - REAPER-parent child-window creation itself is hanging even when delegated to
    a helper window thread inside the new hybrid design
  - notably, that is still different from the older all-helper-thread design,
    which got through `CreateWindowExA` and then died later in `effEditOpen`
  - so there is likely some additional state/ordering requirement from the old
    model that this hybrid split does not preserve

One more targeted follow-up after that:

- The hybrid design was changed so the helper window thread created the older
  wrapper/panel window shape instead of a single `"STATIC"` child, while
  `effEditOpen` still remained on the bridge main thread.
- That regressed much harder:
  - REAPER never reached the startup script
  - no Keepsake debug log was produced for the run
- That makes the conclusion stronger:
  - recreating only the old window shape is not enough
  - the older successful `CreateWindowExA` behavior depended on more of the old
    all-helper-thread flow than just wrapper/panel HWND structure
  - partial reconstruction of that flow appears to make the whole lane less
    stable rather than more stable

Latest continuation after that:

- `bridge_gui_stub_windows.cpp` now supports a runtime-selected embedded-open
  mode via `KEEPSAKE_WIN_EMBED_MODE`:
  - `main`: create the embedded host child on the bridge GUI-owner thread
  - `hybrid`: create the host child on the helper window thread but keep APC
    `effEditOpen` on the bridge GUI-owner thread
  - `window`/`legacy`/`all-helper`: run the full embedded-open path on the
    helper window thread, which is the closest approximation of the older
    all-helper-thread model without rewriting the file again
- The purpose of this switch is not to ship three architectures; it is to let
  the REAPER lane compare call-stack ownership models directly while keeping the
  newer host-safety behavior intact.
- Clean serial REAPER smoke runs against APC with
  `-BridgePathOverride build-win\\Debug\\keepsake-bridge.exe` now show:
  - all three modes keep REAPER responsive enough for the smoke script to reach
    `fx-ui-open-finish` and `fx-ui-close`
  - in every mode, the host-side `EDITOR_SET_PARENT` request still times out
    and Keepsake abandons the live bridge instance instead of retrying another
    risky GUI path on the same instance
  - this is a meaningful safety win even though embedded open is still failing,
    because the host does not lock up
- The visible bridge boundary from those runs is still the same:
  - `gui_open_editor_embedded_impl` enters
  - `IsWindow(parent)` succeeds
  - `get_editor_rect` succeeds
  - the log reaches `before CreateWindowExA host-child ...`
  - then the host-side timeout fires and the bridge is abandoned
- Interpretation:
  - the runtime mode switch is useful because it removes "edit the file again"
    friction from the next comparison step
  - but it also shows that simply choosing between `main`, `hybrid`, and
    restored all-helper-style ownership is not enough to fix APC inside the
    current bridge structure
  - the safety behavior is now doing the right thing: failure to embed no longer
    implies a host lockup

Primary-source follow-up after adding the runtime switch:

- JUCE source confirms two Windows-specific expectations that line up with the
  probe evidence:
  - VST initialization is asserted to happen on the Windows message thread
    because many plugins need to create HWNDs there during init
  - the host's embedded-editor path opens the plugin against an already-owned
    embedded HWND and then immediately performs the usual
    `effEditGetRect -> effEditOpen -> effEditGetRect` sequence on that same
    path
- yabridge's editor architecture points in the same direction at a higher level:
  - it creates an owned editor/windowing surface up front
  - then embeds that owned surface into the host-facing hierarchy
  - then passes the resulting owned Win32 handle to the plugin editor open call
- The important takeaway is not that Keepsake should copy yabridge literally,
  but that both references avoid the "create an ad hoc host-child window at
  `set_parent` time and hope the thread split works out" pattern that Keepsake
  is currently fighting.
- As a small harness improvement, `tools/reaper-smoke.ps1` now accepts
  `-EmbedModeOverride` and forwards it to `KEEPSAKE_WIN_EMBED_MODE`, so mode
  comparisons no longer need an outer environment wrapper command.

First owned-surface redesign result after that:

- The Windows bridge now pre-creates an owned embedded wrapper/panel surface
  before attempting plugin editor open, and only then attaches that surface to
  the host parent with `SetParent()`.
- This directly tests the hypothesis from the JUCE/yabridge research that the
  bridge should own a stable Win32 editor surface before `effEditOpen()` rather
  than trying to create a child window directly under REAPER's parent during
  `EDITOR_SET_PARENT`.
- The first attempt failed for a clean Win32 reason:
  - the pre-created wrapper was mistakenly created with `WS_CHILD` and no
    parent, which returned Win32 error `1406`
  - after switching the pre-created wrapper to start life as a popup/tool
    window and only converting it to a child on attach, the experiment worked
- With that fix in place:
  - `ensure_embedded_surface()` succeeds
  - `attach_embedded_surface()` succeeds
  - REAPER remains responsive
  - `main` mode and `hybrid` mode both now reach
    `effEditOpen begin parent=<owned panel hwnd>`
- This is the clearest architectural progress so far:
  - the old dead zone around creating a host child directly under REAPER's HWND
    is no longer the active blocker
  - the remaining blocker is APC inside `effEditOpen()` after being given the
    bridge-owned embedded panel HWND
- Interpretation:
  - the "owned surface before open" change was worth doing and is closer to the
    structure used by working hosts
  - but it is not sufficient by itself, because APC still wedges in
    `effEditOpen()` even after the parent surface ownership model is improved
  - the next remaining variable is the lifecycle/thread context of the plugin
    instance itself rather than raw host-child window creation

Follow-up proof after instrumenting the current `main` path:

- The bridge now logs the thread ID for:
  - `INIT`
  - direct VST2 load
  - VST2 `effOpen`
  - VST2 `effEditOpen`
- In the current Windows `main` embed path, those are all the same thread for
  APC:
  - `bridge: INIT thread=...`
  - `bridge: INIT load direct thread=...`
  - `bridge/vst2: load begin ... thread=...`
  - `bridge/vst2: effOpen/load complete ... thread=...`
  - `bridge/vst2: effEditOpen begin ... thread=...`
- That is an important negative result:
  - the current `main` path is no longer violating the "load/open on the same
    process thread" expectation suggested by the standalone probe
  - APC still wedges anyway when opened inside the full bridge/REAPER context
- Another targeted comparison also ruled out one more easy explanation:
  - attaching the owned surface to REAPER's root ancestor instead of the direct
    provided wrapper still reaches `effEditOpen begin`
  - so the remaining blocker does not look like a simple "wrong ancestor HWND"
    issue either
- Interpretation:
  - the remaining mismatch is now likely about the full host/context around the
    editor open rather than the narrow thread-affinity or child-window-creation
    seam
  - likely candidates are message-loop expectations during `effEditOpen`,
    visibility/activation/state of the host window hierarchy, or assumptions APC
    makes about being opened from a conventional in-process host window stack

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
   - APC worker-thread load is now strongly suspect too
   - APC worker-thread `effEditGetRect` alone is not the main problem
4. Design a consistent per-instance GUI ownership model:
   - either the bridge process main thread becomes the coherent Windows VST2
     GUI owner for that instance
   - or a stronger "GUI bridge process is disposable" policy is used when the
     owning GUI thread must be allowed to wedge
5. Use JUCE and yabridge source as architectural references, not as copy-paste
   implementations.
6. Treat host-child window creation as a separate variable from `effEditOpen`
   thread affinity:
   - the latest bridge experiment suggests those may have different constraints
7. Treat "window creation thread" and "full embedded-open call stack thread" as
   different variables too:
   - the latest hybrid experiment suggests partial delegation is not equivalent
     to the old all-helper-thread flow
8. Avoid keeping partially reconstructed helper-thread/window-shape experiments
   once they regress scan/load:
   - revert them quickly and preserve only the evidence

Next task: use the new `KEEPSAKE_WIN_EMBED_MODE` switch plus preserved debug-log
runs to compare the full last successful line for `main`, `hybrid`, and
`window` modes without launching REAPER in parallel, then decide whether the
next move should be:
- a stronger disposable-GUI-boundary design in the bridge
- or a redesign where the bridge creates and owns a stable Win32 editor surface
  before `EDITOR_SET_PARENT`, closer to the ownership model used by working
  hosts/reference implementations

## Follow-up: Visibility Control Result

Latest focused APC wrapper-parent runs on `2026-04-14` tightened the visibility
story further:

- The owned wrapper/panel path remains host-safe in `main` mode:
  - REAPER stays responsive
  - the smoke script reaches `fx-ui-open-finish`
  - on timeout, Keepsake abandons the live bridge instead of wedging the host
- In the stable wrapper-parent control run, the bridge now logs:
  - `attach_embedded_surface visible wrapper=0 panel=0 parent=0`
  - `pre-open visibility ... parent_visible=0 ... wrapper_visible=0 panel_visible=0`
  - `effEditOpen begin ...`
- That means APC can still enter `effEditOpen()` even when REAPER's wrapper,
  the bridge-owned wrapper, and the panel are all still `visible=0`.
- A targeted `KEEPSAKE_WIN_FORCE_SHOW_PARENT=1` experiment ruled out one more
  visibility theory:
  - after `wait_for_embed_parent_visibility()` times out, the bridge logs
    `force showing embed parent=...`
  - the next line never appears
  - `send_and_wait(EDITOR_SET_PARENT)` later times out and the host abandons
    the bridge
- Interpretation:
  - forcing `ShowWindow()`/`UpdateWindow()` on REAPER's wrapper is itself a bad
    idea in this context and can block before APC `effEditOpen()` even starts
  - the remaining APC wedge is not simply "the parent must be visible first"
  - host-window visibility is likely owned by REAPER's UI lifecycle and should
    not be forced by Keepsake

Practical consequence:

- Keep the host-safe owned-surface path.
- Do not keep the forced-host-parent-show experiment enabled by default.
- Treat REAPER wrapper visibility as an observation, not a control knob.

Next task: compare the one successful wrapper-parent APC run against the
current stable wrapper-parent timeout run at the host-callback level inside
`effEditOpen()`, because the bridge now reaches APC open again without touching
REAPER's visibility state.

## Follow-up: `audioMasterGetTime` Loop Result

The next focused comparison on `2026-04-14` moved the mystery inside APC's
host-callback behavior during `effEditOpen()`:

- The earlier successful wrapper-parent APC run shows:
  - `effEditOpen begin ...`
  - the expected burst of `BeginEdit` / `Automate` / `EndEdit`
  - `effEditOpen end result=1`
- The current stable timeout run shows:
  - the same `BeginEdit` / `Automate` / `EndEdit` burst
  - then a long repeated loop of `audioMasterGetTime`
  - no `effEditOpen end`
- Important contrast with the standalone probe:
  - the probe host also does not implement `audioMasterGetTime`
  - APC still opens there and never asks for `audioMasterGetTime` during the
    successful current-thread open path
- Targeted negative experiment:
  - temporarily returning a non-null `VstTimeInfo*` from the bridge host
    callback, including a fuller set of requested timing flags, did not change
    the APC outcome
  - APC still wedged after entering the same `audioMasterGetTime` loop
- Interpretation:
  - the bridge-only `audioMasterGetTime` loop is real and useful evidence
  - but it does not appear to be caused by the host merely returning `0` or by
    missing obvious timing fields
  - it is more likely a symptom of the broader embedded-open host context than
    the primary defect itself

Practical consequence:

- Do not keep speculative `audioMasterGetTime` behavior changes unless they
  demonstrate a concrete win.
- Treat the `audioMasterGetTime` loop as a discriminator between the successful
  and failing APC contexts.

Next task: compare the full pre-open host context for the successful
wrapper-parent APC run against the failing wrapper-parent run, especially
window-tree ownership, style/state, and any host callback differences before
the first `audioMasterGetTime`, because that loop now looks like a downstream
symptom rather than the root cause.

## Follow-up: Process Interleaving Result

One more focused comparison on `2026-04-14` exposed a likely contextual
difference, but the first mitigation attempt did not hold up:

- In the successful wrapper-parent APC run:
  - `effEditOpen begin` is followed directly by APC editor callbacks
  - no CLAP/bridge process traffic is visible before `effEditOpen end`
- In the failing wrapper-parent APC run:
  - `effEditOpen begin` is immediately interleaved with:
    - `keepsake: process request ...`
    - `bridge: process request ...`
    - repeated `audioMasterGetTime`
  - APC never returns from `effEditOpen`
- That suggests process/audio activity during embedded open may be part of the
  failing context.

Targeted negative experiment:

- A narrow Windows-only guard was added temporarily to suppress CLAP processing
  while `gui_set_parent()` waited on `EDITOR_SET_PARENT`.
- That experiment regressed the lane badly enough that REAPER could hang before
  the smoke script/debug logs were established.
- The change was reverted completely.

Practical consequence:

- Keep process interleaving in mind as a possible contributor.
- Do not suppress processing from the CLAP side during GUI embed open unless a
  future design makes that coordination explicit and safe.
- The simple "silence while embedding" guard is not robust enough.

Next task: compare the successful wrapper-parent APC run against the failing
wrapper-parent run one level higher than raw window visibility, focusing on
what REAPER/host state differs before `effEditOpen begin`, because both the
visibility-force and process-quiesce local fixes regressed without solving the
underlying mismatch.

## Follow-up: CLAP-Side Parent-Ready Wait Result

The next focused check on `2026-04-14` used a forced fresh workspace build on
the moved Windows VM:

- `tools/reaper-smoke.ps1` was run with:
  - `-ClapPathOverride C:\Users\betterthanclay\keepsake-win\build-win\Debug`
  - `-BridgePathOverride C:\Users\betterthanclay\keepsake-win\build-win\Debug\keepsake-bridge.exe`
- Preserved temp dir:
  - `C:\Users\betterthanclay\AppData\Local\Temp\keepsake-reaper-smoke.fad4315cdeb341b6a1d3014ad7f03002`

Targeted host-side instrumentation:

- `plugin_gui.cpp` now waits up to `750 ms` in `gui_set_parent()` before
  sending `EDITOR_SET_PARENT`.
- During that wait it logs:
  - `IsWindowVisible(hwnd)`
  - whether `WS_VISIBLE` is already set on the REAPER wrapper
  - the raw style value

Result:

- The CLAP side now independently confirms the same state we were seeing from
  inside the bridge.
- From:
  - `keepsake: gui_wait_for_win32_parent_ready start hwnd=... timeout=750`
- Through:
  - repeated `keepsake: gui_set_parent wait parent=... visible=0 style_visible=0 style=0x40000000`
- To:
  - `keepsake: gui_wait_for_win32_parent_ready timeout hwnd=...`
- the REAPER wrapper never becomes visible and never gains `WS_VISIBLE` before
  `EDITOR_SET_PARENT` is sent.
- Only after that timeout does the plugin send:
  - `keepsake: send_and_wait begin opcode=EDITOR_SET_PARENT ...`
- and the bridge later reaches:
  - `bridge/vst2: effEditOpen begin ...`
  - followed by the same repeated `audioMasterGetTime` loop and bridge
    abandonment.

Interpretation:

- This is stronger evidence that the failing APC path is a host-lifecycle
  mismatch, not just a local embed-window bug.
- The bad REAPER wrapper state is already present on the CLAP side before the
  bridge is even asked to embed.
- A short "wait until parent looks ready" delay does not reproduce the earlier
  successful context.

Practical consequence:

- Stop trying to coerce the current wrapper into a good state with more local
  Win32 tweaks or short waits.
- The next productive comparison should move earlier in the lifecycle and
  identify what host-side call/order difference produced the one successful
  wrapper-visible run before `gui_set_parent()` was even entered.

Next task: instrument the higher-level CLAP GUI lifecycle on Windows,
especially the ordering of `gui_set_scale()`, `gui_set_size()`, `gui_show()`,
and `gui_set_parent()`, and compare that against the successful wrapper-visible
run to find the first point where the REAPER host path diverges.

## Follow-up: CLAP GUI Lifecycle Ordering Result

The next forced workspace-binary smoke run on `2026-04-14` added sequence IDs
to the CLAP GUI callbacks in `plugin_gui.cpp`.

Preserved temp dir:

- `C:\Users\betterthanclay\AppData\Local\Temp\keepsake-reaper-smoke.3abb9fbc1e7e4371bfe14e883dcd9b46`

Observed callback order on the failing APC embedded-open path:

1. `keepsake: gui_create seq=1 floating=0 has_editor=1`
2. `keepsake: gui_set_scale seq=2 scale=2.000 -> 0`
3. `keepsake: gui_get_size 960x580`
4. `keepsake: gui_can_resize seq=3 -> 0`
5. `keepsake: gui_set_parent seq=4 handle=... floating=0 open=0`
6. repeated `gui_set_parent wait ... visible=0 style_visible=0 style=0x40000000`
7. `gui_wait_for_win32_parent_ready timeout hwnd=...`
8. `send_and_wait begin opcode=EDITOR_SET_PARENT ...`
9. bridge reaches `effEditOpen begin ...`
10. APC falls into the same repeated `audioMasterGetTime` loop

Important negative result:

- There is still no `gui_show()` callback before `gui_set_parent()` on this
  failing path.
- So the wrapper-visible successful run is not explained by a simple local
  omission inside `gui_set_parent()` itself.
- At least in this REAPER path, embed is being attempted before any CLAP-side
  `show` callback is observed.

Interpretation:

- The strongest remaining hypothesis is now a higher-level host lifecycle
  difference: either the successful run came from a different REAPER callback
  ordering/state transition, or the wrapper-visible state was established by
  REAPER outside the CLAP `gui_show()` call path we are seeing here.

Next task: compare this failing callback order against the earlier successful
wrapper-visible APC run and determine whether REAPER ever issued `gui_show()`
or a materially different GUI callback order there, because that is now the
clearest upstream divergence to verify.

## Follow-up: Staged Parent + `gui_show()` Open Result

The next experiment on `2026-04-14` changed Windows embedded open sequencing to
match the earlier successful APC run more closely:

- `EDITOR_SET_PARENT` now only stages the REAPER wrapper handle in the bridge
  and returns quickly.
- The actual embedded `EDITOR_OPEN` is then triggered from CLAP
  `gui_show()` instead of being executed synchronously inside
  `gui_set_parent()`.

Primary result:

- The call order now matches the earlier good run in the important way:
  1. `gui_set_parent ...`
  2. `gui_set_parent embed staged`
  3. `gui_show seq=... floating=0 open=0`
  4. `send_and_wait begin opcode=EDITOR_OPEN ...`
  5. bridge `gui_open_editor using staged embed parent=...`
- This confirms the old successful run was not a logging illusion: REAPER was
  able to reach `gui_show()` before the bridge attempted embedded
  `effEditOpen()`.

Forced fresh-workspace smoke with the staged-parent path:

- temp dir:
  - `C:\Users\betterthanclay\AppData\Local\Temp\keepsake-reaper-smoke.60a180cba1a643a69148f6084ed80e70`
- Short-timeout result:
  - `gui_show()` reached `EDITOR_OPEN`
  - bridge entered embedded `effEditOpen()`
  - APC still fell into the long `audioMasterGetTime` loop
  - the CLAP side eventually logged:
    - `gui_show() editor open failed`
    - `abandoning bridge ... reason=GUI show editor open failed while bridge still alive`

Long-timeout follow-up:

- temp dir:
  - `C:\Users\betterthanclay\AppData\Local\Temp\keepsake-reaper-smoke.af7615b632444ab4a9d7bfd1395e2335`
- harness result:
  - `fx-ui-open-finish` at about `83.6 s`
  - `fx-ui-close`
  - overall smoke `PASS`
- But the bridge-side pre-open state was still bad:
  - `host-parent ... style=0x40000000 ... visible=0`
  - wrapper/panel also `visible=0`
- So the sequencing change alone did not reproduce the earlier
  wrapper-visible/`WS_VISIBLE` host state before `effEditOpen()`.

Interpretation:

- This is still a meaningful architectural step:
  - the "open during `gui_set_parent()`" path was indeed wrong
  - deferring the real open until `gui_show()` is closer to the host behavior
    that previously worked
- But it is not sufficient by itself.
- The remaining difference now appears to be *when* the bridge performs the
  staged `EDITOR_OPEN` after `gui_show()`:
  - in the current experiment, the bridge still opens immediately from inside
    the `gui_show()` RPC
  - the successful run appears to have allowed more host-side promotion of the
    REAPER wrapper before embedded `effEditOpen()` actually began

Practical consequence:

- Keep pursuing the staged-parent model.
- The next likely improvement is to decouple staged embedded `EDITOR_OPEN`
  from the synchronous `gui_show()` RPC itself, so REAPER can return from
  `gui_show()` and finish promoting the wrapper before APC `effEditOpen()`
  starts.

Next task: prototype a deferred staged embedded open that is requested by
`gui_show()` but executed on the bridge GUI loop after the `EDITOR_OPEN` RPC
returns, then check whether the REAPER wrapper finally reaches the visible
`0x50000000` state before `effEditOpen()`.

## Follow-up: Deferred GUI-Loop Open Result

The next Windows experiment on `2026-04-14` kept the staged-parent model but
stopped opening embedded immediately inside the synchronous `EDITOR_OPEN` /
`gui_show()` RPC:

- `gui_show()` still sends `EDITOR_OPEN`
- but the bridge now only queues the staged embedded open
- the actual `gui_open_editor_embedded_impl()` runs on the next bridge
  `gui_idle()` pass

This was the first clear end-to-end improvement.

Preserved smoke run:

- `C:\Users\betterthanclay\AppData\Local\Temp\keepsake-reaper-smoke.5cdccb099ea445a3ac834877ca0f0c6f`

Observed sequence:

1. `gui_set_parent seq=4 ...`
2. `gui_set_parent embed staged`
3. `gui_show seq=5 floating=0 open=0`
4. bridge `gui_open_editor queued staged embed parent=...`
5. host receives `send_and_wait OK opcode=EDITOR_OPEN`
6. `gui_show success seq=6 floating=0 open=1`
7. bridge `gui_idle opening staged embed parent=...`
8. bridge `pre-open visibility ... parent_visible=1 ... wrapper_visible=1 ... panel_visible=1`
9. bridge `host-parent ... style=0x50000000 ... visible=1`
10. `effEditOpen begin ...`
11. `effEditOpen end result=1 ...`
12. `embedded editor open end ok=1`
13. `editor embedded in host window ...`
14. `gui_idle staged embed result=1`

Harness result:

- `fx-ui-open-finish` at about `9.9 s`
- `fx-ui-close`
- overall smoke `PASS`

Important interpretation:

- Deferring the real embedded open until *after* the synchronous
  `gui_show()` RPC returns was the missing host-lifecycle step.
- This is the first run where the bridge itself observed the REAPER wrapper in
  the same good state as the earlier historical success:
  - `style=0x50000000`
  - `visible=1`
- The previous staged-parent-but-synchronous-open experiment improved
  sequencing but still opened too early; the wrapper remained
  `style=0x40000000`, `visible=0` there.

Practical consequence:

- The Windows VST2 APC embedded-open path should keep this deferred staged-open
  architecture.
- The next work should be stabilization and cleanup rather than more blind
  host-window experiments.

Next task: validate the deferred staged-open architecture against Serum and the
existing Windows smoke lanes, then reduce the temporary investigation logging
once the cross-plugin checks stay stable.

## Follow-up: Standalone Probe Expansion Result

The standalone Windows VST2 probe was expanded on `2026-04-14` so it can now
exercise more than just load + editor open. It now supports:

- activation (`effSetSampleRate`, `effSetBlockSize`, `effMainsChanged`)
- chunk read/write (`effGetChunk`, `effSetChunk`)
- simple processing (`processReplacing`)
- a small overlap case where chunk read happens while processing is active

Primary purpose:

- stop debugging inside the full REAPER + CLAP + bridge lifecycle when we need
  to answer smaller host-behavior questions first
- establish which combinations are safe in a tiny VeSTige host before
  reintroducing bridge complexity

Observed results in the expanded standalone host:

- Serum:
  - `open_editor + activate + get_chunk + set_chunk + process` all succeeded
  - `chunk_during_process` also succeeded
- APC:
  - `open_editor + activate + get_chunk + set_chunk + process` all succeeded
  - `chunk_during_process` also succeeded

Important interpretation:

- The tiny host still does **not** reproduce the catastrophic REAPER lockup,
  even when we overlap state (`effGetChunk`) and active processing.
- Both plugins repeatedly query `audioMasterGetTime` during processing in this
  standalone host too, and that alone is not enough to break them.
- That strongly reinforces the current hypothesis that the dangerous behavior
  is introduced by the higher-level REAPER/CLAP/bridge lifecycle rather than
  by basic VST2 hosting primitives themselves.

Practical consequence:

- The standalone harness is now a good proving ground for the next smaller
  experiment instead of continuing to thrash inside the full host path.
- The next useful extension is not more window-debugging; it is a closer
  miniature of the REAPER failure shape, such as overlapping editor-open,
  processing, and state access in a controlled sequence.

Next task: extend the standalone probe with one or two explicit "editor +
processing + chunk overlap" scenarios so we can see which minimal combination
starts to resemble the REAPER lockup before we touch the main bridge again.

## Follow-up: Editor-In-The-Loop Probe Result

The standalone probe was extended again on `2026-04-14` with two more explicit
overlap scenarios:

- `open_during_process`
  - start processing first
  - then call `effEditOpen()` while processing is already active
- `editor_chunk_during_process`
  - open the editor first
  - then overlap active processing with `effGetChunk()`

Observed results:

- Serum:
  - `open_during_process` succeeded
  - `editor_chunk_during_process` succeeded
- APC:
  - `open_during_process` succeeded
  - `editor_chunk_during_process` succeeded

Important interpretation:

- Even the more REAPER-like miniature scenarios still do **not** reproduce the
  catastrophic host lockup.
- Both plugins continue to spam `audioMasterGetTime` during processing in these
  runs, and that still is not sufficient to break them.
- That means the dangerous behavior now looks even less like a raw
  "VST2 plugin cannot tolerate editor/process/state overlap" problem and more
  like a specific interaction introduced by the full Keepsake bridge path,
  host callback surface, or CLAP/REAPER lifecycle integration.

Practical consequence:

- The standalone harness has now ruled out a larger slice of the obvious
  overlap theories.
- The next useful standalone extension would be a miniature closer to bridge
  semantics rather than plain host semantics, such as adding a coarse
  request/response boundary or delayed host callback behavior to see whether
  the lockup requires the extra IPC-style timing we introduce in Keepsake.

Next task: use the standalone probe to simulate a more "bridge-like" host
surface or timing boundary, because plain in-process editor/process/chunk
overlap is still not enough to reproduce the REAPER failure.

## Follow-up: Bridge-Gate Standalone Probe Result

The standalone probe was extended again on `2026-04-14` with a coarse
"bridge gate" mode:

- all plugin entry points used by the probe now optionally pass through one
  serialized timed gate
- the same runs can also enable the earlier bridge-like callback surface:
  - `audioMasterGetTime -> 0`
  - bridge-like `audioMasterGetAutomationState`
  - small callback delays
- this gives the tiny host a much closer approximation of the bridge's
  "one owned call boundary plus extra latency" behavior without dragging
  REAPER or CLAP back into the experiment

Representative commands:

- `tools/windows-vst2-probe.ps1 "C:\Program Files\Common Files\VST2\Serum_x64.dll" -OpenEditor -Activate -OpenDuringProcess -ProcessBlocks 200 -ProcessSleepMs 1 -BridgeHostMode -BridgeGateMode -CallbackDelayMs 2 -GetTimeDelayMs 2 -GateDelayMs 2`
- `tools/windows-vst2-probe.ps1 "C:\Program Files\Ample Sound\APC.dll" -OpenEditor -Activate -OpenDuringProcess -ProcessBlocks 200 -ProcessSleepMs 1 -BridgeHostMode -BridgeGateMode -CallbackDelayMs 2 -GetTimeDelayMs 2 -GateDelayMs 2`
- `tools/windows-vst2-probe.ps1 "C:\Program Files\Common Files\VST2\Serum_x64.dll" -OpenEditor -Activate -EditorChunkDuringProcess -GetChunk -ProcessBlocks 200 -ProcessSleepMs 1 -BridgeHostMode -BridgeGateMode -CallbackDelayMs 2 -GetTimeDelayMs 2 -GateDelayMs 2`
- `tools/windows-vst2-probe.ps1 "C:\Program Files\Ample Sound\APC.dll" -OpenEditor -Activate -EditorChunkDuringProcess -GetChunk -ProcessBlocks 200 -ProcessSleepMs 1 -BridgeHostMode -BridgeGateMode -CallbackDelayMs 2 -GetTimeDelayMs 2 -GateDelayMs 2`

Observed results:

- Serum:
  - `open_during_process` still succeeded
  - `editor_chunk_during_process` still succeeded
- APC:
  - `open_during_process` still succeeded
  - `editor_chunk_during_process` still succeeded

Important interpretation:

- Even after adding both:
  - a bridge-like host callback surface
  - a coarse serialized call gate with extra delay
- the tiny host still does **not** reproduce the catastrophic REAPER lockup.
- That makes the problem look even less like "basic plugin overlap plus a bit
  of latency" and more like a higher-level bridge integration issue:
  - cross-process request/response ownership
  - CLAP/REAPER callback ordering
  - host-side waits interacting with bridge-side waits

Practical consequence:

- The standalone harness has now ruled out another plausible class of causes.
- The next useful miniature is no longer just "more latency" or "more mutex";
  it needs to model an actual command boundary:
  - one thread acting like the host issuing synchronous requests
  - another thread acting like the plugin side servicing them
  - explicit waits around open/process/chunk so we can see whether the failure
    shape only appears once requests have to cross a real rendezvous boundary

Next task: extend the standalone probe with a tiny request/response command
loop so editor/process/chunk calls can be marshalled across a fake bridge
boundary, because plain in-process serialization plus callback delay is still
not enough to reproduce the REAPER failure.

## Follow-up: Marshalled Command-Boundary Probe Result

The standalone probe was extended again on `2026-04-14` with a tiny
fake-bridge command loop:

- one service thread owns the VST instance
- the host thread sends synchronous requests to that service thread
- representative commands:
  - `query_rect`
  - `open_editor`
  - `process`
  - `get_chunk`
- this is intentionally much closer to the real bridge shape than the earlier
  "same process + mutex" experiments

The same bridge-like host surface was kept enabled:

- `audioMasterGetTime -> 0`
- callback delays
- optional serialized gate inside the service thread

Observed results:

- `open_during_process`:
  - Serum succeeded
  - APC succeeded
- `editor_chunk_during_process`:
  - Serum hit a host-side command timeout waiting for `open_editor`
  - APC hit the same host-side command timeout waiting for `open_editor`
  - in both cases, the service thread later logged `effEditOpen result=1`
    after about `4.0-4.6 s`

Important interpretation:

- This is the **first** standalone result that meaningfully diverges from the
  plain in-process probe.
- The plugin is not hard-deadlocking in the miniature:
  - `effEditOpen()` eventually returns success
- but the synchronous rendezvous boundary materially changes the timing:
  - editor open becomes slow enough that a host-side wait budget can expire
    even though the plugin itself eventually completes
- That is much closer to the real bridge failure shape than anything the probe
  had shown earlier.

Why this matters:

- It suggests the dangerous part may be less "plugin wedges forever" and more
  "host waits synchronously across a command boundary while the bridge-side
  editor open takes longer than expected"
- Once the host has already timed out or changed state, the later bridge-side
  success may be too late and can poison the lifecycle

Practical consequence:

- The probe now supports the core architectural suspicion:
  - the rendezvous boundary itself is part of the problem
  - not just VST2 overlap, callback shape, or a local mutex
- This makes the next standalone experiment very concrete:
  - compare command-boundary open latency with and without child HWND creation
  - compare "open first" vs "query chunk first" vs "show/idle before open"
  - identify which part of the boundary is stretching `effEditOpen()` from
    sub-second to multi-second behavior

Next task: use the marshalled probe to isolate why `effEditOpen()` becomes a
4-4.6 s operation once it crosses the fake bridge boundary, because that is
the first miniature result that actually resembles the real bridge timing
problem.

## Follow-up: Plain Marshalled Open Result

The marshalled probe was tightened again on `2026-04-15`:

- pending and completed command state were split so a timed-out host request
  can no longer corrupt the next queued request
- this made the plain `open_editor` case trustworthy in marshal mode

Observed results with:

- `--open-editor --activate --bridge-host-mode --bridge-gate-mode --bridge-marshal-mode`
- `--callback-delay-ms 2 --get-time-delay-ms 2 --gate-delay-ms 2`

APC:

- `effEditGetRect` completed in about `1.7 s`
- plain marshalled `open_editor` timed out host-side after `9.0 s`
- the service thread later logged:
  - `effEditOpen 9078.9 ms result=1`
- meaning APC eventually completed editor open, but only after the host-side
  synchronous wait had already expired

Serum:

- `effEditGetRect` still completed quickly (`3.4 ms`)
- plain marshalled `open_editor` timed out host-side after `11.0 s`
- unlike APC, no later `effEditOpen result=1` line appeared even after an
  additional observation window
- practical reading: Serum appears to stay stuck once plain editor open crosses
  the fake bridge boundary

Important interpretation:

- We no longer need process/chunk overlap to reproduce the timing hazard.
- A **plain synchronous editor-open request across the fake bridge boundary**
  is enough to trigger the bad shape:
  - APC: "late success after host timeout"
  - Serum: "host timeout with no observed completion"
- That is much closer to the real Keepsake/REAPER failures than any earlier
  standalone experiment.

Practical consequence:

- The core danger now looks architectural:
  - synchronous host-side waits for editor open across a rendezvous boundary
    are inherently unsafe for these plugins
- This supports the broader design rule the user cares about:
  - Keepsake must not let the host block on bridge-side editor open

Next task: extend the standalone probe one more step to model the safer design
we probably need in Keepsake: fire editor-open asynchronously across the fake
bridge boundary and let the host observe completion later, instead of waiting
inside the request path.

## Follow-up: Async Marshalled Open Result

The standalone probe was extended again on `2026-04-15` with an async open
mode across the fake bridge boundary:

- the host thread posts an `OpenEditorAsync` command
- the service thread still performs the real `effEditOpen()` itself
- the host does **not** synchronously wait for the open request to return
- instead it polls shared completion state during an observation window

Representative commands:

- `tools/windows-vst2-probe.ps1 "C:\Program Files\Ample Sound\APC.dll" -OpenEditor -Activate -BridgeHostMode -BridgeGateMode -BridgeMarshalMode -AsyncOpenMarshalMode -CallbackDelayMs 2 -GetTimeDelayMs 2 -GateDelayMs 2 -IdleMs 15000 -OpenTimeoutMs 9000`
- `tools/windows-vst2-probe.ps1 "C:\Program Files\Common Files\VST2\Serum_x64.dll" -OpenEditor -Activate -BridgeHostMode -BridgeGateMode -BridgeMarshalMode -AsyncOpenMarshalMode -CallbackDelayMs 2 -GetTimeDelayMs 2 -GateDelayMs 2 -IdleMs 15000 -OpenTimeoutMs 9000`

Observed results:

APC:

- async open was posted immediately
- service thread logged:
  - `effEditOpen 312.7 ms result=1`
- host observed:
  - `open_completed=1`
  - `open_result=1`

Serum:

- async open was posted immediately
- service thread logged:
  - `effEditOpen 710.9 ms result=1`
- host observed:
  - `open_completed=1`
  - `open_result=1`

Important interpretation:

- Once the host stopped synchronously waiting inside the request path, the
  fake bridge boundary no longer produced the catastrophic timing shape.
- Both plugins completed editor open successfully across the same artificial
  boundary that previously caused:
  - APC late success after host timeout
  - Serum apparent stuck open with host timeout
- This is the strongest miniature confirmation so far that the unsafe piece is
  the **synchronous host-side wait on bridge-side editor open**, not merely the
  existence of a bridge boundary by itself.

One caveat:

- the follow-up `CloseEditor` command still timed out in this async probe lane,
  so the miniature is not yet fully tidy
- but that does not change the main result: async host behavior removed the bad
  open-path timing shape

Practical consequence:

- The safer Keepsake architecture now has direct probe support:
  - do not let the host block synchronously on bridge-side editor open
  - request editor open, return to the host, and observe completion later

Next task: translate this probe result back into Keepsake by redesigning the
Windows GUI/editor-open path so the host never waits synchronously for bridge
editor open to finish.

## Follow-up: Serum Validation Result

The deferred staged-open architecture also held up on Serum in REAPER.

Standalone probe sanity check:

- `tools/windows-vst2-probe.ps1 "C:\Program Files\Common Files\VST2\Serum_x64.dll" -OpenEditor -Parent child -LoadThread current -RectThread current -OpenThread current`
- Serum still behaves in the tiny host on the known-good
  current-thread/current-thread/current-thread path.

REAPER smoke validation:

- command used the existing Serum plugin ID:
  - `keepsake.vst2.58667358`
- preserved temp dir:
  - `C:\Users\betterthanclay\AppData\Local\Temp\keepsake-reaper-smoke.3583d6a1900b421bb9f4c715a744b7ae`
- harness result:
  - `fx-add-finish`
  - `fx-ui-open-finish` at about `10.5 s`
  - `fx-ui-close`
  - overall `PASS`

Meaning:

- The deferred staged-open path is no longer just an APC-specific win.
- It survives the second main regression plugin from the Windows handoff lane.
- That makes the core lifecycle fix much more credible as the right
  architecture, not just a plugin-specific workaround.

Next task: keep APC + Serum as the primary Windows GUI regressions, then trim
the temporary investigation logging to a smaller persistent set while
preserving the new staged/deferred embedded-open architecture.

## Follow-up: Host-Safe And Playable Smoke Result

The next Windows bridge change removed the audio starvation that still remained
after the host-safety work.

Key bridge change:

- in `src/bridge_loader_vst2.cpp`, audio-side `effProcessEvents` and
  `processReplacing` no longer wait behind the same `effect_mutex` while
  `effEditOpen()` is already in progress
- this is intentionally narrow to the editor-open window

Meaning:

- the host-side safety work already prevented REAPER from locking up
- but the bridge was still "safe but silent" because the editor-open path
  starved audio processing
- allowing audio-side VST2 calls to keep flowing during `effEditOpen()`
  removed that starvation in the smoke harness

Serial smoke evidence on the fresh build:

- Serum transport/audio:
  - `C:\Users\betterthanclay\AppData\Local\Temp\keepsake-reaper-smoke.52ced3029084485180e6f61695790976`
  - `audio-peak max=0.331733 saw_audio=1`
  - overall `PASS`
- APC transport/audio:
  - `C:\Users\betterthanclay\AppData\Local\Temp\keepsake-reaper-smoke.c3878bc7080447c3b918f0a5edef6285`
  - `audio-peak max=0.376650 saw_audio=1`
  - overall `PASS`
- installed-path serial sanity:
  - Serum transport/audio:
    - `C:\Users\betterthanclay\AppData\Local\Temp\keepsake-reaper-smoke.8c6c2b026d8c43108e3c3daab4807c6c`
    - `PASS`
  - APC open/close:
    - `C:\Users\betterthanclay\AppData\Local\Temp\keepsake-reaper-smoke.e10976c9cc774f8ba0f4253745296625`
    - `PASS`

Important caution:

- some earlier APC/Serum scan-timeout artifacts were produced by parallel smoke
  runs and should not be treated as trusted evidence
- the serial runs above are the trustworthy ones

Current status:

- the Windows smoke lane is now both host-safe and playable for APC and Serum
- however, manual REAPER still needs fresh confirmation because there was still
  a user-reported immediate Serum lockup before this checkpoint

Next task: validate the exact same installed build in a manual REAPER session,
then continue investigating any remaining manual-only mismatch from there.
