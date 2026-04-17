# Roadmap Generation Index

Status: active
Updated: 2026-04-17

## Active generation

- `g03`

## Generation log

| Generation | Started | Reason | Notes |
|---|---|---|---|
| `g01` | 2026-04-10 | Initial roadmap sequence | Baseline generation for the initial build, CLAP factory, VeSTige loader, and proof-of-concept work |
| `g02` | 2026-04-12 | Alpha release stream | g01 proved the bridge architecture and CI lane. g02 is the release-hardening generation: narrow claims to proven scope, close docs drift, define artifacts, collect evidence, and publish `v0.1-alpha`. |
| `g03` | 2026-04-17 | Post-alpha stabilization | `v0.1-alpha` is published. g03 is the short follow-through generation for release-window bug intake, claim corrections, install/runtime hardening, and evidence refresh where the public surface shifts. |

## Rollover policy

Create a new generation only when maintainers explicitly decide the sequencing baseline needs a real reset.

Generations should be substantial. As a healthy default, expect something closer to 20 to 40 roadmap files before rollover is worth discussing. Treat that as a judgment guardrail, not an automatic counter.

Rollover is a closeout event, not a convenience move. Before opening the next generation:

- close, pause, supersede, or rehome every roadmap in the current generation
- refresh the roadmap front doors so the old generation is visibly closed
- purge stale generation-specific specs and batch cards from `docs/specs/` so the active planning tree no longer carries dead lane debris

If that cleanup has not happened, stay in the current generation and finish the closeout there first.

## Next task

Execute `g03.001` — convert the first real post-release bug cluster into a
stabilization batch instead of reopening release-cut work.
