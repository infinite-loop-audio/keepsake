# G02.003 — Alpha Validation Matrix and Evidence Pack

Status: complete
Owner: Infinite Loop Audio
Updated: 2026-04-17
Governing refs:
  - docs/contracts/001-working-rules.md
  - docs/contracts/002-clap-factory-interface.md
  - docs/contracts/004-ipc-bridge-protocol.md
  - docs/contracts/006-process-isolation-policy.md
Auto-continuation: allowed within g02 once g02.001-002 are complete

## Scope

Prove the alpha claim set with a written validation matrix and fresh evidence.
This milestone converts "we think it works" into "we have current release
evidence for what we claim."

The matrix should be driven by the scope from g02.001, not by every code path
that exists in the repository.

## Steps

### 1. Write the validation matrix

Create a release matrix covering the actual alpha claims:

- platform
- host
- format
- plugin sample(s)
- install
- scan/discovery
- instantiate
- params
- state
- GUI open/close
- audio / transport
- crash/timeout behavior where relevant

Acceptance:
- every claimed support lane has explicit checks
- every non-claimed lane is marked experimental, deferred, or out-of-scope

### 2. Run the primary alpha lane

At minimum, rerun the strongest evidence lane from a release perspective:

- installable artifact
- real host
- real plugins
- discovery
- instantiate
- params
- GUI
- save/load if in claimed scope

Expected strongest lane today:
- macOS
- REAPER
- VST2
- APC + Serum repro plugins

Acceptance:
- the primary alpha lane has fresh release-window evidence

### 3. Run any additional claimed lanes

If g02.001 claims Windows, Linux, VST3, AU, or 32-bit support for alpha,
capture evidence for them here. If the evidence is weak or incomplete, narrow
the claim set instead of inventing confidence.

Best near-term secondary lane if an Ubuntu VM is available:

- Ubuntu x86_64
- REAPER
- repo `test-plugin.so`
- install `keepsake.clap`
- verify discovery, instantiate, params, GUI open/close, short transport

This is the preferred first Linux host-validation lane because it avoids
blocking on a third-party Linux VST2 hunt while still exercising a real host
and real bridge path.

Acceptance:
- no published support claim is left without current evidence

### 4. Freeze the known-issues list

Update the alpha known-issues surface from actual validation output:

- confirmed limitations
- flaky cases
- unsupported hosts/plugins
- manual workarounds

Acceptance:
- known issues are backed by release-window testing, not stale memory

## Evidence Requirements

- release validation matrix doc
- dated log entry for each meaningful validation batch
- CI run reference for the release candidate commit
- commands and hosts actually used
- explicit PASS/FAIL/unsupported status per lane

## Closeout

Closed by:

- packaged macOS candidate artifact smoke in REAPER for APC, Serum, and Khords
- refreshed `docs/releases/v0.1-alpha-validation-matrix.md`
- refreshed `docs/known-issues-v0.1-alpha.md`
- current green push CI at `24551205423`

## Stop Conditions

- stop if a claimed lane cannot be validated in the current release window
- stop if a host/plugin failure changes the intended alpha scope materially

## Next Task

Execute g02.004 — publish `v0.1-alpha` only after the evidence pack matches the
claimed support envelope.
