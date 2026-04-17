# Roadmaps

Roadmaps are executable milestone plans derived from vision, architecture, and
contracts.

## Active Generation

- `g03` — post-alpha stabilization: release-window bug intake, claim
  corrections, and install/runtime hardening on top of the published alpha

## Generation Index

- [`generation-index.md`](generation-index.md)

## Layout

- `g01/` — first generation milestones
- `g02/` — alpha release generation
- `g03/` — post-alpha stabilization generation
- `generation-index.md` — active generation and rollover history
- `backlog/` — deferred items with promotion criteria
- `templates/roadmap-milestone-template.md` — milestone starter contract

## Status

**g01 sequencing intent met.** The core bridge, GUI, scan robustness, CI, and
codebase-health lanes are complete enough to justify a release-focused
generation rollover.

**g02 is complete.** `v0.1-alpha` is now published.

**g03 is now active.** This generation is the short post-release stabilization
lane: bug intake, claim corrections, install/runtime friction, and targeted
evidence refresh without pretending the next step is immediate scope widening.

## Batch and Logging Rule

- Execute milestones in meaningful batches.
- Create logs per completed batch or update cycle, not per individual task.
- Stop execution when a batch reveals a missing contract, missing repo
  authority, or other planning gap.

## Next Task

Execute `g03.001` — capture the first post-alpha stabilization batch from the
published `v0.1-alpha` surface.
