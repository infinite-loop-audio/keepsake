# 001 - Working Rules

Status: active
Owner: Infinite Loop Audio
Updated: 2026-04-10
Depends on: docs/architecture/system-architecture.md
Authority owners: Infinite Loop Audio
Affects: all execution work in this repo

## Problem

Keepsake is a cross-platform C/C++ project with legal non-negotiables (VeSTige
only, no Steinberg SDK), platform-specific crash isolation requirements, and
a need for clear execution grammar so agent sessions and contributors know when
to proceed and when to stop. Without explicit working rules, the legal
guardrails and done-ness criteria are only aspirational.

## Contract

### Delivery grammar

- Material work should follow this chain:
  `vision -> research/specs -> architecture + contracts -> roadmap milestone -> execution -> evidence -> closeout`.
- Use separate contracts for stable seams, important boundaries, or durable
  rules that need their own authority surface.
- Use specs only while shaping material changes; promote durable outcomes into
  architecture and contracts before roadmap execution depends on them.
- Once those outcomes are promoted, treat specs as provisional planning history
  rather than canonical authority; keep, archive, or remove them based on
  whether they still help the active lane.
- A ready batch card should define scope, steps, governing refs, acceptance
  criteria, evidence requirements, stop conditions, and whether
  auto-continuation is allowed.
- In a strict lane, a bare `continue` should resolve through the previous
  closeout's `Next Task`, which should normally point at the current ready card
  or an explicit stop/reassessment step.

### Intent checkpoints

- When planning is needed and the next direction is not clearly determined by
  the current authority surfaces, stop and ask the operator for intent instead
  of inventing the next lane or batch.
- Treat competing plausible directions, milestone handoff choices, and still-open
  product tradeoffs as intent checkpoints rather than routine planning work.
- Do not mark a card `ready` while an unresolved intent checkpoint still
  governs its scope.

### Legal guardrail (hard stop)

- Stop immediately and raise to the operator if any proposed implementation
  path would require using or referencing the Steinberg VST2 SDK.
- This is a legal constraint, not a preference. VeSTige only.
- Stop immediately if a proposed change would make CLAP no longer the outer
  plugin format (e.g., switching to a VST3 outer format).

### Execution guardrail pack

- prefer real integrated behavior over mockups, placeholders, or token
  scaffolding
- prefer simplicity over decorative or architectural complexity that the
  governing refs do not require
- prefer end-to-end follow-through over convenient partial closure when a batch
  promised a working path
- prefer explicit incompleteness over implied completion when a path is still
  scaffolded or unproven
- treat disconnected gesture work as incomplete unless the batch was explicitly
  scoped as bounded substrate-only work

### Definition of done

- Do not call work done while it is still a mockup, placeholder, or partial
  token implementation.
- Update dependent refs, roadmap state, and logs so they match reality.
- Update any front-door or currentness surfaces that still name the active
  lane, current ready card, or recent evidence chain so they do not keep
  advertising stale authority after closeout.
- Run the required validation commands and record them in a log.
- Name unresolved blockers or limits explicitly instead of hiding them inside a
  completion claim.
- Leave one explicit next task that makes a later bare `continue` unambiguous.

### Execution autonomy

- Agents may continue across consecutive ready batch cards without waiting for
  a manual prompt.
- Auto-continuation is allowed only when the cards stay inside the same active
  lane, the governing refs still match, and the prior card's evidence gate
  passed.
- Set a local upper bound for uninterrupted runs, such as a card limit or time
  limit, so autonomy remains bounded.

### Automation runtime policy

- Prefer `effigy` when it already covers the repo operation.
- When repo-owned script logic is still needed, default to `TypeScript` run
  with `bun`.
- Use `bash` only for thin glue or compatibility boundaries that Effigy or
  Bun/TypeScript cannot own cleanly.
- Use `python` or another runtime only when a concrete technical requirement
  justifies it.
- Build tooling (CMake, Ninja, etc.) is C/C++ project infrastructure, not a
  repo scripting exception.

### Generation posture

- Treat roadmap generations as substantial sequencing eras, not one-or-two-file buckets.
- Keep one generation active across many milestones until the sequencing baseline itself needs a reset.
- Treat roughly 20 to 40 milestones as the normal scale of a healthy generation before rollover is even worth discussing.
- Treat rollover as full generation closeout:
  - every roadmap in the old generation must be explicitly closed, superseded, or moved to backlog
  - the roadmap front doors must reflect that closed state before the next generation opens
  - stale specs and batch cards from the closing generation must be archived or removed from `docs/specs/`
- If those closeout conditions are not satisfied, repair the current generation instead of opening a new one.

### Stop conditions

- stop on planning gaps, contradictions, or missing authority
- stop when operator intent or prioritization is still unresolved across
  multiple plausible planning directions
- stop when user-facing ambiguity exceeds the project guardrails
- stop when validation fails in a way that changes the plan
- stop when the current card is complete and the next one is not already ready
- stop (hard) when any path would violate the legal guardrails above

### Currentness surfaces

- Keep the repo's front-door currentness surfaces aligned to the active lane:
  - `docs/README.md`
  - `docs/contracts/contract-index.md`
  - `docs/roadmaps/README.md`
  - `docs/roadmaps/generation-index.md`
  - `docs/roadmaps/g01/README.md` (current generation)
  - `docs/specs/README.md` when specs are part of the lane
  - `docs/logs/README.md`
- When a card closes, those surfaces must either:
  - point at the next ready card or active milestone state
  - or explicitly say the lane is awaiting reassessment
- Do not leave a completed card named as the current ready card after closeout.

## Validation

- `effigy qa`
- `effigy qa:northstar`

## Roadmap Impact

- All milestones in g01 and beyond

## Planning Notes

These working rules exist because Keepsake has a legal constraint (VeSTige
only) that must never be violated by any implementation decision, and because
the project will need clear execution grammar once active development begins
and agent sessions are used for implementation work.

## Next Task

Author the first roadmap milestone (CLAP factory proof-of-concept and VeSTige
loader scaffold) and create the CLAP factory interface contract.
