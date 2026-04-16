# Logs

Logs capture dated evidence and assessments.

## Segmentation Model

- Group logs by month directory: `YYYY-MM/`
- Name each log: `DD-HHMMSS-<slug>.md`

## Cadence Rule

- Create logs per completed batch or update cycle.
- Do not create a separate log for every task.

## Recent Evidence

- [`2026-04/16-235950-macos-live-x64-strap-closeout.md`](2026-04/16-235950-macos-live-x64-strap-closeout.md) — x64 macOS live windows now carry the internal Keepsake strap and close cleanly in the harness
- [`2026-04/16-235900-macos-live-editor-posture-closeout.md`](2026-04/16-235900-macos-live-editor-posture-closeout.md) — g02.005 closeout: Serum/APC/Khords validated in REAPER live mode; embedded preview demoted to diagnostic-only posture
- [`2026-04/16-235500-macos-live-editor-host-batch.md`](2026-04/16-235500-macos-live-editor-host-batch.md) — g02.005 batch 5.2 partial: auto/live operator surface landed; Serum/APC live harness pass, Khords live open still failing
- [`2026-04/16-233000-macos-live-editor-fallback-contract.md`](2026-04/16-233000-macos-live-editor-fallback-contract.md) — g02.005 batch 5.1 complete: explicit macOS preview/live/auto contract and harness proof for preview + live paths
- [`2026-04/16-221500-mac-iosurface-embedded-ui-architecture-decision.md`](2026-04/16-221500-mac-iosurface-embedded-ui-architecture-decision.md) — accepted cutoff: current mac cross-process embedded-input model is not a universal support path
- [`2026-04/11-093000-g01-009-32-bit-bridge.md`](2026-04/11-093000-g01-009-32-bit-bridge.md) — g01.009 complete: packed IPC structs, PE/ELF arch detection, 32-bit bridge routing
- [`2026-04/11-090000-g01-008-multi-format-loaders.md`](2026-04/11-090000-g01-008-multi-format-loaders.md) — g01.008 complete: bridge loader abstraction, AU v2 scanning + loading, multi-format factory
- [`2026-04/10-230000-g01-006-gui-forwarding.md`](2026-04/10-230000-g01-006-gui-forwarding.md) — g01.006 complete: floating window GUI, macOS Cocoa editor hosting, CLAP GUI extension
- [`2026-04/10-225000-g01-005-midi-params-state.md`](2026-04/10-225000-g01-005-midi-params-state.md) — g01.005 complete: note events, parameter automation, state save/restore
- [`2026-04/10-223000-g01-004-scan-cache-config.md`](2026-04/10-223000-g01-004-scan-cache-config.md) — g01.004 complete: scan caching, config.toml, rescan sentinel
- [`2026-04/10-220000-g01-003-cross-platform-cross-arch.md`](2026-04/10-220000-g01-003-cross-platform-cross-arch.md) — g01.003 complete: platform abstraction, x86_64 bridge under Rosetta, Windows/Linux backends
- [`2026-04/10-214500-g01-002-audio-bridge-subprocess.md`](2026-04/10-214500-g01-002-audio-bridge-subprocess.md) — g01.002 complete: subprocess bridge, IPC protocol, shared memory audio, CLAP plugin lifecycle
- [`2026-04/10-210000-g01-001-clap-factory-vst2-loader.md`](2026-04/10-210000-g01-001-clap-factory-vst2-loader.md) — g01.001 complete: build system, CLAP factory, VeSTige loader, architecture detection

## Templates

- `templates/roadmap-contract-delta-template.md`
- `templates/decision-log-template.md`

## Next Task

Add g02 release-window evidence as meaningful batch logs: alpha scope/docs
closeout, packaging/versioning work, validation matrix runs, and final release
publication proof.
