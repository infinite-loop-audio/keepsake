# Product Guardrails

Status: active
Owner: Infinite Loop Audio
Updated: 2026-04-10
Vision refs: docs/vision/001-keepsake-vision.md

## Operator Experience

- Keep the operator path clear and direct; do not require unnecessary navigation
  or invented side flows just to complete normal work.
- Prefer repo-owned facts, active roadmap state, and current logs over generic
  process theatre.

## UI and Workflow Simplicity

- Do not add UI or interaction complexity unless the governing refs make the
  user need explicit.
- Prefer simple, readable, maintainable flows over decorative sophistication or
  surface-area growth.
- Keepsake has no UI beyond a settings/preferences panel for scan path config
  and rescan triggers. Do not invent UI features not required by the vision.

## Legal Guardrails (Non-Negotiable)

- The Steinberg VST2 SDK must never appear in this repository.
- Only VeSTige (LGPL v2.1, clean-room ABI) is permitted as the VST2 ABI
  surface.
- The VST3 SDK (GPLv3 or proprietary) may be used for VST3 hosting, but
  the VST3 loader must run in a separate subprocess so the license boundary
  is at the process/IPC edge. Resolve GPLv3 compatibility before VST3 ships.
- CLAP remains the outer plugin format. Do not introduce a VST3 outer format —
  the VST3 SDK licence explicitly excludes VST2 hosting.
- Do not use the VST Compatible logo or claim Steinberg certification.
- All references to "the VST2 binary plugin format" must be descriptive and
  nominative. Include the trademark attribution where appropriate.

## Anti-Fake-Work Rules

- Do not call mockups, placeholders, fake adapters, or token substrate work
  "done" when the batch was supposed to land working behavior.
- Do not leave disconnected gesture work behind and imply the real path now
  exists.
- If a seam is still scaffolded or unproven, name it explicitly as incomplete.
- "Works" means: loads, initializes, and exposes a plugin through the CLAP
  factory with correct metadata. Not: compiles with no errors.

## Delivery Expectations

- Prefer integrated end-to-end follow-through over convenient partial stopping
  points.
- Update canonical refs, roadmap state, and logs so they match reality before
  claiming closure.
- Completion requires real evidence, not only plausible prose.

## Autonomy Expectations

- Agents may continue only across ready cards inside the same valid lane with
  current governing refs.
- Agents must stop on planning gaps, contradictions, failed validation, missing
  authority, or user-facing ambiguity that exceeds these guardrails.
- Stop immediately if a proposed change would introduce Steinberg SDK usage,
  VST3 outer format, or any content that violates the legal constraints above.

## Specs Posture

- Use `specs/` while a material change is still being shaped.
- Once durable outcomes are accepted, promote structure into architecture and
  behavior or policy into contracts.
- Keep specs only while they still help the active lane or provide useful
  planning history; archive or remove them when they no longer add value.

## Next Task

Keep these guardrails reflected in contracts, specs, roadmaps, and logs so
they remain enforceable rather than aspirational.
