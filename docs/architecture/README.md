# Architecture

Architecture docs define the system shape and invariants derived from vision.
They set the realized constraints roadmap batches must honor.

## Files

- [`system-architecture.md`](system-architecture.md) — component layout and
  CLAP/VeSTige seams
- [`system-inventory.md`](system-inventory.md) — execution-relevant surfaces
- [`product-guardrails.md`](product-guardrails.md) — delivery guardrails and
  anti-fake-work rules

## Writing Rules

- Link architecture updates to the current vision artifact
  (`docs/vision/001-keepsake-vision.md`).
- Promote durable structural decisions out of specs into architecture before
  roadmap execution relies on them.
- Keep `system-inventory.md` current so roadmap work only starts against
  explicitly planned system elements.
- Keep milestone execution lists in roadmap files, not architecture files.
- Use contract docs for explicit technical boundaries that need validation and
  migration notes.

## Next Task

Fill `system-architecture.md` and `system-inventory.md` with the initial CLAP
plugin factory model and VeSTige loading surfaces once the first roadmap
milestone is drafted.
