# Docs

## Start Here

- [`vision/README.md`](vision/README.md)
- [`architecture/README.md`](architecture/README.md)
- [`contracts/README.md`](contracts/README.md)
- [`roadmaps/README.md`](roadmaps/README.md)
- [`logs/README.md`](logs/README.md)

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

G01 complete. The full pipeline works: config → scan → cache → factory → bridge
→ audio. Both arm64 and x86_64 VST2 plugins work on Apple Silicon. Platform
abstraction covers macOS, Windows, and Linux. Next: assess g02.
