# G02.001 — Alpha Scope, Claims, and Docs Reconciliation

Status: ready
Owner: Infinite Loop Audio
Updated: 2026-04-12
Governing refs:
  - docs/contracts/001-working-rules.md
  - docs/contracts/002-clap-factory-interface.md
  - docs/contracts/004-ipc-bridge-protocol.md
  - docs/contracts/006-process-isolation-policy.md
  - docs/project-brief.md
Auto-continuation: allowed within g02

## Scope

Define what `v0.1-alpha` actually claims, then make the repo say that
consistently. The codebase is ahead of the public docs: README, architecture,
inventory, roadmap front doors, and release posture still read like an early
proof-of-concept even though the bridge, CI, and real-host smoke lane are now
working.

This milestone sets the alpha support envelope and closes the biggest docs
drift before any packaging or publishing work starts.

This milestone does **not** publish binaries or create the release tag. It is
the truth-setting pass that everything else depends on.

## Steps

### 1. Define the alpha support envelope

Write the explicit support posture for `v0.1-alpha`:

- which platforms are supported vs experimental
- which formats are supported vs experimental
- which hosts have real evidence behind them
- what the current GUI/editor expectations are by platform
- whether 32-bit support is in-scope for the alpha claim set

Decision rule:
- prefer narrow, proven scope over broad aspirational wording
- if a lane lacks evidence, mark it experimental or leave it out

Acceptance:
- one authoritative alpha scope statement exists in docs
- unsupported/experimental lanes are named explicitly

### 2. Reconcile public docs with reality

Update the front-door docs so they no longer advertise stale early-stage
posture:

- `README.md`
- `docs/README.md`
- `docs/vision/README.md`
- `docs/architecture/system-architecture.md`
- `docs/architecture/system-inventory.md`
- `docs/roadmaps/README.md`
- `docs/contracts/contract-index.md`

Key fixes:
- remove "early development" placeholders that are no longer true
- replace `draft` / `planned` / `TBD` claims that the code has already settled
- update current posture and next-task text to point at the alpha stream

Acceptance:
- front-door docs describe the current bridge and release lane accurately
- no public-facing file still claims the IPC model or format loaders are TBD

### 3. Publish the alpha known-issues baseline

Create the first real alpha known-issues list:

- host cache quirks
- format limitations
- platform limitations
- GUI/editor limitations
- unproven or lightly tested lanes
- any release caveats like unsigned binaries or manual install friction

This list becomes required input for release notes and packaging.

Acceptance:
- known issues exist as a dedicated surfaced doc or release-ready section
- every support caveat in the alpha scope has a matching operator-readable note

### 4. Tighten roadmap posture

Update roadmap and generation surfaces so `continue` resolves cleanly through
the release stream rather than falling back into completed g01 prose.

Acceptance:
- g02 is clearly the active lane
- the next move after this milestone is packaging/versioning, not vague polish

## Evidence Requirements

- updated doc diffs for all touched authority/front-door surfaces
- one explicit alpha scope statement
- one explicit known-issues surface
- `effigy qa`
- `effigy doctor`

## Stop Conditions

- stop if the alpha scope cannot be stated honestly without fresh validation
- stop if missing contract work is needed before docs can truthfully describe a
  boundary

## Next Task

Execute g02.002 — package the alpha around the newly defined scope: versioning,
artifacts, install docs, and release notes scaffolding.
