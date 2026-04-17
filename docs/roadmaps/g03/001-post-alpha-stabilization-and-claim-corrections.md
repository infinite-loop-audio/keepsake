# G03.001 — Post-Alpha Stabilization and Claim Corrections

Status: active
Owner: Infinite Loop Audio
Updated: 2026-04-17
Governing refs:
  - docs/contracts/001-working-rules.md
  - docs/releases/v0.1-alpha.md
  - docs/releases/v0.1-alpha-validation-matrix.md
  - docs/known-issues-v0.1-alpha.md
Auto-continuation: allowed within g03

## Scope

Handle the first real post-release wave after `v0.1-alpha`.

This milestone exists to absorb what the public release exposes:

- bug reports from real installs and hosts
- claim corrections if any release wording is too broad or too soft
- installer friction and runtime regressions on the published artifacts
- evidence refresh if the strongest claimed lane shifts materially

This is a stabilization lane, not a quiet feature stream.

## Steps

### 1. Triage release-window reports

Capture the first meaningful reports against the published alpha:

- install failures
- scan failures
- GUI/runtime regressions
- host-specific lockups
- config/documentation confusion

Acceptance:
- real incoming issues are grouped into clear classes
- each class has an owner path: fix now, document now, or defer

### 2. Correct claims quickly

If public wording outruns evidence, narrow it immediately instead of waiting
for a larger docs sweep.

Acceptance:
- README, release notes, matrix, and known issues stay aligned to reality

### 3. Land the first stabilization fixes

Fix the highest-value release-window regressions in meaningful batches.

Bias:
- primary supported lane first
- installer/runtime breakage before polish
- narrow, proven fixes over speculative churn

Acceptance:
- at least one meaningful post-release stabilization batch lands with evidence

### 4. Refresh evidence where the release surface moved

If a stabilization fix changes the actual supported posture, refresh the
validation surface behind that claim.

Acceptance:
- release-window evidence remains current enough to defend the public posture

## Evidence Requirements

- dated log for each meaningful stabilization batch
- release doc / known-issues / matrix diffs where claims changed
- commands and hosts used for any refreshed evidence

## Stop Conditions

- stop if a reported problem reveals a missing contract or architecture gap
- stop if the intended fix is really a new generation-scale feature

## Next Task

Take the first real post-alpha report cluster and turn it into the opening
stabilization batch rather than letting release follow-up scatter into ad hoc
fixes.
