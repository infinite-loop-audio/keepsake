---
title: Keepsake thread-001 handoff
status: active
owner: Infinite Loop Audio
updated: 2026-04-10
tags: [coordination, handoff]
---

## What This Thread Was Doing

This thread originated inside the Signal/Loophole project as a research arc investigating whether VST2 host support was legally viable for Signal. The investigation concluded firmly: no, not directly in Signal — Steinberg discontinued the VST2 SDK in October 2018, closed all new license agreements, and has on-record statements that new VST2 host products are not permitted without a pre-2018 signed license. Signal does not hold one.

The thread then pivoted to the question of whether the user need could be met cleanly at all. The answer was yes, through the established open-source pattern of VeSTige + LGPL — the same basis LMMS and Ardour have used for over 20 years without successful enforcement. The outcome was a decision to create a separate standalone open-source CLAP plugin project: Keepsake.

The rest of the thread was foundation work for that new project: naming research (Keepsake cleared, Heirloom ruled out due to Spitfire Audio conflict), legal posture decisions, architecture decisions, org placement, and installation of the full Northstar docs spine with the stricter delivery layer. The thread ended with all baseline planning surfaces in place but no roadmap milestones authored and no code written.

## Why It Matters

Signal's users have real legacy plugins — synths, compressors, effects — that exist only in VST2 format and will never be ported. Signal cannot host them directly without a licence Signal does not hold. Keepsake is the clean path: it is a CLAP plugin (Signal already hosts CLAP natively), it is a separate standalone open-source project that Signal does not bundle or distribute, and it is grounded in the same VeSTige legal lineage that LMMS and Ardour have depended on for two decades.

Without Keepsake, Signal users with large VST2 plugin libraries either stay on old hosts or lose access to irreplaceable tools. With it, those plugins appear in the Signal plugin browser with no special Signal code required.

## Current State

- Done so far:
  - All key architecture and legal decisions resolved (see Important Context below)
  - Northstar baseline docs spine installed and passing `effigy qa:northstar`
  - Stricter delivery layer (`contracts/001-working-rules.md`, `specs/`) installed
  - `docs/project-brief.md` written — full legal basis, architecture description, Signal integration tiers
  - `docs/vision/001-keepsake-vision.md` written — long-horizon outcome, constraints, success criteria
  - `docs/architecture/system-architecture.md` written — component layout, key seams, prior art
  - `docs/architecture/product-guardrails.md` written — legal non-negotiables, anti-fake-work rules, delivery expectations
  - `docs/contracts/001-working-rules.md` written — delivery grammar, intent checkpoints, legal hard stops, done-ness criteria, autonomy rules
  - `docs/contracts/contract-index.md` written — lists pending contracts needed before g01.001 can execute
  - `docs/roadmaps/g01/README.md` written — g01 sequencing intent and scope boundary
  - `README.md` and `AGENTS.md` written — public-facing project description and agent orientation
  - `effigy doctor` passes (ok:15, warn:1 — no `health` task defined, non-blocking)

- Still open:
  - `docs/roadmaps/g01/001-clap-factory-and-vst2-loader-poc.md` — first roadmap milestone, not yet authored
  - `docs/contracts/002-clap-factory-interface.md` — CLAP factory interface contract, not yet authored (blocks g01.001 execution)
  - `docs/contracts/003-vestige-loader-abi.md` — VeSTige loader ABI contract (can be written alongside g01.001 or as a prerequisite)
  - No code written — the repo is planning-only at this point
  - IPC bridge protocol contract deferred to g01.002+
  - Platform config format contract deferred to g01.001+ once format is chosen

- Active spec lane: none — no provisional specs are active; all baseline material has been promoted directly into architecture and contracts

- Canonical refs for execution:
  - `/Users/betterthanclay/Dev/projects/keepsake/docs/contracts/001-working-rules.md`
  - `/Users/betterthanclay/Dev/projects/keepsake/docs/architecture/system-architecture.md`
  - `/Users/betterthanclay/Dev/projects/keepsake/docs/architecture/product-guardrails.md`
  - `/Users/betterthanclay/Dev/projects/keepsake/docs/vision/001-keepsake-vision.md`
  - `/Users/betterthanclay/Dev/projects/keepsake/docs/project-brief.md`

- Remaining continuation envelope: the next thread should author g01.001 and contract 002 — these are not yet ready cards but are the clear next authoring step before any execution begins

- Lane budget / pause signal: the previous thread closed cleanly. No continuation envelope is in-flight. The next thread starts fresh on authoring.

- Key files:
  - `/Users/betterthanclay/Dev/projects/keepsake/AGENTS.md`
  - `/Users/betterthanclay/Dev/projects/keepsake/docs/README.md`
  - `/Users/betterthanclay/Dev/projects/keepsake/docs/project-brief.md`
  - `/Users/betterthanclay/Dev/projects/keepsake/docs/vision/001-keepsake-vision.md`
  - `/Users/betterthanclay/Dev/projects/keepsake/docs/architecture/system-architecture.md`
  - `/Users/betterthanclay/Dev/projects/keepsake/docs/architecture/product-guardrails.md`
  - `/Users/betterthanclay/Dev/projects/keepsake/docs/contracts/001-working-rules.md`
  - `/Users/betterthanclay/Dev/projects/keepsake/docs/contracts/contract-index.md`
  - `/Users/betterthanclay/Dev/projects/keepsake/docs/roadmaps/README.md`
  - `/Users/betterthanclay/Dev/projects/keepsake/docs/roadmaps/g01/README.md`

## Boundaries

- Stay within g01 planning and initial proof-of-concept scope — no crash isolation, caching, or scan infrastructure until the CLAP factory and VeSTige loader are proven working
- Do not use or reference the Steinberg VST2 SDK under any circumstances — this is a legal hard stop, not a preference; raise to the operator immediately if any implementation path requires it
- Do not make CLAP anything other than the outer plugin format — VST3 outer format is prohibited because the VST3 SDK licence explicitly excludes VST2 hosting use
- Do not begin implementation on g01.001 until `docs/contracts/002-clap-factory-interface.md` exists and the milestone itself is authored as a ready card
- Do not treat "compiles without errors" as done — the g01.001 milestone closes when a real VST2 plugin appears in a real CLAP host with correct metadata
- Signal integration work (Tier 2+ polish: badge, rescan trigger, detection) belongs in the Signal repo, not here; do not introduce Signal dependencies into Keepsake
- Follow repo constraints from [AGENTS.md](/Users/betterthanclay/Dev/projects/keepsake/AGENTS.md)

## Important Context

**Planning lineage:**
The project was conceived in April 2026 out of a Signal research thread. The legal and architecture decisions were settled in that thread before the repo was created. The Northstar spine was installed in the same session that created the repo. All roadmap milestones, contracts, and code remain to be authored. This thread is the first Keepsake thread and there are no prior Keepsake logs to consult.

**Key decisions and their rationale (all settled — do not re-litigate without new information):**

| Decision | Choice | Why |
|---|---|---|
| Language | C/C++ | VeSTige is a C header; no reason to constrain to Rust |
| Outer plugin format | CLAP (MIT) | VST3 SDK licence explicitly excludes VST2 hosting; CLAP has no such exclusion |
| VST2 ABI basis | VeSTige (LGPL v2.1) | Clean-room headers; 20+ year LMMS/Ardour precedent; never Steinberg SDK |
| Architecture pattern | CLAP plugin factory | Each VST2 plugin appears as a distinct CLAP entry; Carla is the closest prior art |
| Out-of-process hosting | Subprocess per plugin | Crash isolation; study Carla's approach |
| Plugin ID namespace | `keepsake.vst2.*` | Stable detection key for Signal and other CLAP hosts |
| Licence | LGPL v2.1 | Matches VeSTige lineage |
| Project name | Keepsake | Cleared for trademark/product clashes April 2026; Heirloom ruled out (Spitfire Audio conflict) |
| Org | `infinite-loop-audio` GitHub org | Deliberate; separation is at the code boundary, not the org boundary |
| Signal relationship | Zero Signal code changes for basic function | Keepsake is a well-behaved CLAP plugin; Signal hosts CLAP natively |

**Pending contracts (must be authored before g01.001 execution):**

| Contract | Boundary | Status |
|---|---|---|
| `002-clap-factory-interface.md` | Descriptor shape, plugin ID namespace, factory lifecycle | Not yet authored |
| `003-vestige-loader-abi.md` | Which VeSTige entrypoints Keepsake calls and how | Not yet authored |
| `004-ipc-bridge-protocol.md` | Subprocess lifecycle, crash handling, message format | Deferred to g01.002+ |
| `005-platform-config-format.md` | config.toml schema and scan path semantics | Deferred to g01.001+ once format decided |

**Prior art to study before implementation:**
- LMMS VeSTige integration — canonical reference for VeSTige usage (20+ years)
- Carla — closest architectural reference for CLAP/LV2 factory model that exposes bridged plugins
- Ardour VST2 support — secondary VeSTige-lineage reference

**effigy doctor output (live, 2026-04-10):**
- ok:15, warn:1 — the only warning is a missing `health` task (non-blocking, no remediation needed before g01 work begins)

**Open tensions:**
- The IPC mechanism for the out-of-process subprocess model is TBD — Carla is the study target but the actual mechanism (pipes, shared memory, sockets) is not yet chosen; this shapes the `004-ipc-bridge-protocol.md` contract but does not block g01.001 authoring
- Apple Silicon / Rosetta 2 complexity for x86_64 VST2 plugins is known and documented as a VST2 limitation, not a Keepsake one; no design decisions required yet

## Suggested Next Move

Start by running the orientation sequence in the repo:

```sh
effigy tasks
effigy doctor
```

Then read (in order):
1. `docs/contracts/001-working-rules.md` — the execution grammar the next thread must follow
2. `docs/contracts/contract-index.md` — the missing contracts that block g01.001 execution
3. `docs/roadmaps/g01/README.md` — the g01 sequencing intent and scope boundary
4. `docs/architecture/system-architecture.md` — the component layout and key seams

Then author these two artifacts as a batch (they should be done together before any implementation begins):

1. `docs/contracts/002-clap-factory-interface.md` — define descriptor shape, plugin ID namespace (`keepsake.vst2.*`), factory lifecycle (when the factory is queried, how descriptors are returned, what the host can expect)
2. `docs/roadmaps/g01/001-clap-factory-and-vst2-loader-poc.md` — first roadmap milestone covering: build system setup (CMake / CLAP SDK / VeSTige dependency wiring), CLAP factory stub with one hardcoded descriptor, VeSTige loader proof-of-concept loading one real VST2 binary, CLAP factory integration exposing discovered plugin as a descriptor, and manual test evidence (plugin appears in a real CLAP host)

The milestone should reference contract 002 as a governing ref and should not be marked ready until that contract exists.

## Completion Protocol

1. The previous thread closed with all baseline Northstar surfaces installed and passing qa. No batch card was in-flight at closing — this handoff is the closing artifact.
2. Roadmap state: `docs/roadmaps/g01/README.md` accurately reflects "no milestones yet, next: create 001-clap-factory-and-vst2-loader-poc.md".
3. No continuation envelope is in-flight from the previous thread. The next thread starts fresh.
4. No explicit pause signal — the previous thread completed its scope (foundation setup). The natural next action is milestone authoring.
5. Unresolved risks:
   - IPC mechanism for subprocess model is not yet chosen (no blocker for g01.001 authoring, but must be resolved before g01.002 execution)
   - Apple Silicon / Rosetta 2 complexity is documented but not yet designed around
   - VeSTige ABI edge cases in real-world VST2 plugins are unknown until implementation begins; LMMS and Carla source are the study targets
6. Next task: author `docs/contracts/002-clap-factory-interface.md` and `docs/roadmaps/g01/001-clap-factory-and-vst2-loader-poc.md` as a paired batch.
