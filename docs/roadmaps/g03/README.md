# G03 — Post-Alpha Stabilization

Status: active
Started: 2026-04-17

## Milestones

| Milestone | Title | Status |
|---|---|---|
| `001` | [Post-Alpha Stabilization and Claim Corrections](001-post-alpha-stabilization-and-claim-corrections.md) | active |

## Sequencing Intent

G03 starts after `v0.1-alpha` publication.

This generation is not about broadening claims quickly. It is the short,
practical stabilization lane that follows a first public drop:

- bug intake from real users
- claim corrections where release wording outruns evidence
- installer/runtime friction on the published artifact surface
- targeted validation refresh where new regressions or weak spots show up

The goal is to make the published alpha less surprising before opening another
scope-widening or feature-heavy generation.

## Release Posture

- `v0.1-alpha` is published and is now the reference surface.
- Supported lane remains `macOS + REAPER + VST2`.
- Windows, Linux, VST3, AU v2, and 32-bit remain experimental until fresh
  evidence says otherwise.
- Post-release bugs beat roadmap curiosity.

## Next Task

Execute `g03.001` — capture the first post-alpha stabilization batch: triage
release-window bug reports, correct any overstated claims, and tighten the
published install/runtime posture where needed.
