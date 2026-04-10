# G01 — Initial Build and Proof-of-Concept

Status: planning
Started: 2026-04-10

## Milestones

No milestones yet.

Next: create `001-clap-factory-and-vst2-loader-poc.md`.

## Sequencing Intent

G01 covers the work from zero to a working CLAP plugin that exposes at least
one VST2 plugin as a CLAP descriptor in a real host. It ends when:

- the build system works on all three platforms
- the CLAP factory and VeSTige loader are implemented and working
- the out-of-process hosting model is proven (even if not production-hardened)
- scanning, caching, and rescan triggering work at a basic level

G01 does not end just because the proof-of-concept compiles. It ends when a
real VST2 plugin appears in a real CLAP host.

## Next Task

Create the first milestone: `001-clap-factory-and-vst2-loader-poc.md`.
