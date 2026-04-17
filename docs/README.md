# Docs

## Start Here

- [`vision/README.md`](vision/README.md)
- [`architecture/README.md`](architecture/README.md)
- [`contracts/README.md`](contracts/README.md)
- [`roadmaps/README.md`](roadmaps/README.md)
- [`logs/README.md`](logs/README.md)
- [`releases/README.md`](releases/README.md)
- [`known-issues-v0.1-alpha.md`](known-issues-v0.1-alpha.md)

Stricter delivery layer (installed):

- [`contracts/001-working-rules.md`](contracts/001-working-rules.md)
- [`specs/README.md`](specs/README.md)

Project reference material:

- [`project-brief.md`](project-brief.md)

## Working Rule

The baseline docs spine is `vision/`, `architecture/`, `contracts/`,
`roadmaps/`, and `logs/`.

Keepsake also uses the stricter delivery spine: `contracts/001-working-rules.md`
and `specs/` so execution grammar and batch-card planning are explicit once
development work begins.

Start in `vision/` for long-horizon direction, `architecture/` and `contracts/`
for the canonical structure and rules, `roadmaps/` for active execution
sequencing, and `logs/` for batch evidence and decisions.

If `specs/` exists, treat it as provisional planning that should promote into
`architecture/` and `contracts/` before execution relies on it.

## Current Posture

G01 is complete. The core bridge lanes exist: config → scan → cache → factory
→ bridge → audio, plus GUI, CI, and codebase-health follow-through.

`v0.1-alpha` is now published.

G03 is active. This is the short post-alpha stabilization stream:

- take real release-window bug reports seriously
- correct claims quickly if public wording outruns evidence
- tighten install/runtime behavior on the published artifact surface
- refresh evidence only where the public posture actually moves

The strongest current proof is still the macOS + REAPER + VST2 lane. Treat
broader platform/format support as experimental until g02 validation says
otherwise.

The strongest current proof is still the published `macOS + REAPER + VST2`
lane. Treat broader platform/format support as experimental until fresh
stabilization evidence says otherwise.

## Next Task

Execute `g03.001` — absorb the first post-alpha report cluster into a real
stabilization batch.
