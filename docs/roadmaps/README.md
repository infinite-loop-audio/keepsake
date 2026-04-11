# Roadmaps

Roadmaps are executable milestone plans derived from vision, architecture, and
contracts.

## Active Generation

- `g01` — Initial build, CLAP factory, VeSTige loader, and first working
  proof-of-concept

## Generation Index

- [`generation-index.md`](generation-index.md)

## Layout

- `g01/` — first generation milestones
- `generation-index.md` — active generation and rollover history
- `backlog/` — deferred items with promotion criteria
- `templates/roadmap-milestone-template.md` — milestone starter contract

## Status

**g01 sequencing intent met.** All four milestones complete:
- g01.001: CLAP factory + VeSTige loader
- g01.002: Audio bridge + subprocess isolation
- g01.003: Cross-platform + cross-architecture
- g01.004: Scan cache + configuration

## Batch and Logging Rule

- Execute milestones in meaningful batches.
- Create logs per completed batch or update cycle, not per individual task.
- Stop execution when a batch reveals a missing contract, missing repo
  authority, or other planning gap.

## Next Task

Ship g01.005 (MIDI/params/state) — makes bridged plugins actually usable
for music production.
