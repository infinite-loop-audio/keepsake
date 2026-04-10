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

Keepsake is a standalone open-source CLAP plugin (C/C++) that bridges VST2
legacy plugins into CLAP-capable hosts using VeSTige clean-room headers.
Published by Infinite Loop Audio under LGPL v2.1. Related to Signal/Loophole
but deliberately separate — Signal ships zero VST2 code and does not depend on
Keepsake.

## Key Boundaries

- Do not use or reference the Steinberg VST2 SDK — only VeSTige is permitted.
- CLAP is the outer plugin format (MIT licensed, no VST3 licence conflicts).
- Platform targets: macOS (x86_64 + arm64), Windows (x86_64), Linux (x86_64).
- Legal posture and VeSTige rationale live in `docs/project-brief.md` and
  `docs/architecture/`.
