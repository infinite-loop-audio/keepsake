# Docs

## Start Here

- [`vision/README.md`](vision/README.md)
- [`architecture/README.md`](architecture/README.md)
- [`contracts/README.md`](contracts/README.md)
- [`roadmaps/README.md`](roadmaps/README.md)
- [`logs/README.md`](logs/README.md)
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

G02 is active. This is the `v0.1-alpha` release stream:

- define the honest support envelope
- close docs drift
- package artifacts
- collect release-window evidence
- publish from a known-issues posture

The strongest current proof is still the macOS + REAPER + VST2 lane. Treat
broader platform/format support as experimental until g02 validation says
otherwise.
