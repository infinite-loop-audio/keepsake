# Roadmaps

Roadmaps are executable milestone plans derived from vision, architecture, and
contracts.

## Active Generation

- `g02` — `v0.1-alpha` release stream: support envelope, packaging, validation,
  and publication

## Generation Index

- [`generation-index.md`](generation-index.md)

## Layout

- `g01/` — first generation milestones
- `g02/` — alpha release generation
- `generation-index.md` — active generation and rollover history
- `backlog/` — deferred items with promotion criteria
- `templates/roadmap-milestone-template.md` — milestone starter contract

## Status

**g01 sequencing intent met.** The core bridge, GUI, scan robustness, CI, and
codebase-health lanes are complete enough to justify a release-focused
generation rollover.

**g02 is now active.** This generation does not assume broad support claims by
default. It converts the current working state into an honest alpha release by
defining supported scope, closing docs drift, packaging artifacts, collecting
evidence, and publishing from explicit known-issues posture.

## Batch and Logging Rule

- Execute milestones in meaningful batches.
- Create logs per completed batch or update cycle, not per individual task.
- Stop execution when a batch reveals a missing contract, missing repo
  authority, or other planning gap.

## Next Task

Execute g02.004 — publish `v0.1-alpha` from the now-aligned release surface and
evidence pack.
