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

No milestones yet. The first milestone should cover:

1. Build system setup (CMake / CLAP SDK / VeSTige dependency)
2. CLAP plugin factory stub — single hardcoded descriptor
3. VeSTige loader proof-of-concept — load one VST2 `.dll`/`.so`/`.dylib`
4. CLAP factory integration — expose discovered plugin as a descriptor
5. Manual test: plugin appears in a CLAP host

This is the proof-of-concept gate before crash isolation, scanning, and caching
work begins.

## Batch and Logging Rule

- Execute milestones in meaningful batches.
- Create logs per completed batch or update cycle, not per individual task.
- Stop execution when a batch reveals a missing contract, missing repo
  authority, or other planning gap.

## Next Task

Create `g01/001-clap-factory-and-vst2-loader-poc.md` as the first roadmap
milestone.
