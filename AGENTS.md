# Keepsake AGENTS

This file applies to the whole repository.

## Start Here

```sh
effigy tasks
effigy doctor
effigy test --plan
```

Then prefer `effigy <task>` for supported repo work before falling back to raw
tools.

## Default Loop

```sh
effigy tasks
effigy doctor
effigy test --plan
effigy qa
```

Use `--repo <PATH>` only when you intentionally want to target a different
repository.

## Docs Authority

- [`docs/README.md`](docs/README.md)
- [`docs/vision/README.md`](docs/vision/README.md)
- [`docs/roadmaps/README.md`](docs/roadmaps/README.md)
- [`docs/logs/README.md`](docs/logs/README.md)
- [`docs/contracts/001-working-rules.md`](docs/contracts/001-working-rules.md)
- [`docs/specs/README.md`](docs/specs/README.md)

## Repo Context

Keepsake is a standalone open-source CLAP plugin (C/C++) that bridges legacy
plugins — VST2, VST3, and AU v2, including 32-bit binaries — into CLAP-capable
hosts with process isolation. VST2 uses VeSTige clean-room headers. Published
by Infinite Loop Audio under LGPL v2.1. Related to Signal/Loophole but
deliberately separate — Signal ships zero legacy bridge code and does not
depend on Keepsake.

## Key Boundaries

- Do not use or reference the Steinberg VST2 SDK — only VeSTige is permitted
  for VST2.
- VST3 SDK (GPLv3) is permitted for VST3 hosting, in a subprocess only.
- CLAP is the outer plugin format (MIT licensed, no VST3 licence conflicts).
- 32-bit bridging is a first-class architectural concern.
- Platform targets: macOS (x86_64 + arm64), Windows (x86_64), Linux (x86_64).
- Legal posture and format-specific rationale live in `docs/project-brief.md`
  and `docs/architecture/`.

## Internal Writing Style

Use the repo-local style reference for internal work and normal replies:

- `docs/policy/internal-writing-style.md`

<!-- BEGIN EFFIGY AGENT CONTRACT -->
## Effigy Agent Contract

Use Effigy as the default command surface for supported project work.

Route by job, not by startup ritual:
- use `effigy graph` for code understanding
- use `effigy tasks` for selector inventory
- use `effigy doctor` for routing ambiguity or repo health
- use `effigy test --plan` when test execution shape matters

Use `effigy graph` when the job is code understanding: ownership, flow,
implementation, or changed-file impact. Do not insert graph into unrelated
deployment, state, docs, release, or direct task-execution work.

Prefer `effigy <task>`, `effigy test`, and the matching built-in surface over
raw package-manager or shell commands when Effigy covers the path. Use
`effigy --json <command>` whenever another agent or tool will consume output.

This repo's local `.agents/skills/effigy` copy is authoritative for this
project. When an agent supports both project-local and global skills, prefer
the project-local copy over any globally installed Effigy skill.

Do not add `--repo .` while already inside the target repo. Do not edit
`.github/workflows/` or run release mutations unless the user explicitly asks.

Reference docs:
- Effigy agent adoption: `docs/guides/047-agent-and-cross-repo-adoption.md`
- Graph workflows: `docs/guides/076-code-graph-and-agent-workflows.md`
- JSON contracts: `docs/guides/017-json-output-contracts.md`
<!-- END EFFIGY AGENT CONTRACT -->
