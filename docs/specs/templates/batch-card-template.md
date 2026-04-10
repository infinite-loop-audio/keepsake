# <NNN> - <Batch Card Title>

Status: draft
Owner: <owner>
Updated: YYYY-MM-DD
Master spec refs: <docs/specs/NNN-...>
Governing refs: <architecture files>, <contract files>, <roadmap refs>
Auto-start next card: <yes/no/conditional>

## Ready-State Checks

- [ ] Objective is bounded enough to finish without fresh planning decisions.
- [ ] Governing refs point at current canonical surfaces.
- [ ] Scope boundaries and stop conditions are explicit.
- [ ] Acceptance criteria, validation, and evidence requirements are explicit.
- [ ] No unresolved planning gap still governs this card.
- [ ] No unresolved intent checkpoint still governs this card.
- [ ] If auto-start is allowed, the continuation envelope is explicit and the
      next card is already defined and ready or the condition is explicit and
      already satisfiable.

## Objective

State the exact bounded outcome for this card.

## Scope

- <in scope>
- Do not <out of scope>

## Steps

1. <step>
2. <step>

## Acceptance Criteria

- <criterion>

## Evidence Required

- <command/check/log>

## Continuation Envelope

- Auto-start next card: <yes/no/conditional>
- In-bounds next card: <card ref or none>
- Remaining ready chain after this card: <0/1/2/...>
- Transition proof required before auto-start: <validation/evidence/none>

## Lane Budget

- Current card ends budgeted run: <yes/no>
- Further operator decision required after this card: <yes/no>
- Pause signal if run stops here: <budget-exhausted/stop-signal-fired/lane-complete/handoff-required/none>

## Stop Conditions

- <stop condition>
- Ask for operator intent if a real planning branch or milestone handoff choice
  appears that is not already settled in the governing refs.

## Completion Notes

Record what changes once this card is complete.

## Closeout Sequence

- [ ] Update this card's status and completion notes first.
- [ ] Update the active roadmap milestone if progress, readiness, or the next
      batch changed.
- [ ] Update front-door currentness surfaces that name the active lane, ready
      card, or recent evidence chain so they no longer point at stale state.
- [ ] Write the batch log with evidence, validation actually run, and
      unresolved blockers or limits.
- [ ] Record whether the continuation envelope still holds or a stop signal
      exhausted it.
- [ ] Record the lane budget state and pause signal when the run is not simply
      continuing in-bounds.
- [ ] Create or update a handoff only if another thread truly needs to take
      over.
- [ ] Leave one explicit next task in the highest-authority active surface
      that still governs the lane.

## Next Task

State the next ready card or promotion step unlocked by this card.
In a strict repo, this should be the default target of a later bare
`continue` unless the lane is explicitly stopping for reassessment.
