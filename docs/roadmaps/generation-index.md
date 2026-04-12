# Roadmap Generation Index

Status: active
Updated: 2026-04-12

## Active generation

- `g02`

## Generation log

| Generation | Started | Reason | Notes |
|---|---|---|---|
| `g01` | 2026-04-10 | Initial roadmap sequence | Baseline generation for the initial build, CLAP factory, VeSTige loader, and proof-of-concept work |
| `g02` | 2026-04-12 | Alpha release stream | g01 proved the bridge architecture and CI lane. g02 is the release-hardening generation: narrow claims to proven scope, close docs drift, define artifacts, collect evidence, and publish `v0.1-alpha`. |

## Rollover policy

Create a new generation when:
- manually triggered by maintainers based on sequencing needs
- typically after a major vision/architecture or contract shift, or when
  roadmap scale warrants a new boundary

Generations are expected to be long-lived. Do not open `g02` just because one
or two milestones landed; prefer rollover only when the sequencing baseline
itself needs a reset.

## Next task

Execute g02.001 — define the `v0.1-alpha` support envelope, reconcile repo
claims with verified behavior, and turn release scope into explicit roadmap and
known-issues inputs.
