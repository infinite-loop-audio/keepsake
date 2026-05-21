---
name: effigy
description: >
  Effigy task-runner skill for agents: route by job instead of ritual —
  use graph for code-navigation questions, tasks for selector inventory,
  doctor for routing or health ambiguity, test --plan when test shape matters,
  run work through effigy selectors, parse --json envelopes, and avoid
  release/CI footguns. Use when the user mentions effigy, effigy.toml, or
  needs tests, dev, QA, repo navigation, deployment, or validation in an
  Effigy-adopting repo.
metadata:
  internal: true
---

# Effigy Skill

## What Effigy is

One CLI (`effigy`) + **`effigy.toml`** (often split across includes) = the
repo's task surface. **Selectors** (`dev`, `api/test`, `qa:ci:fast`) route to
manifest tasks or **built-ins** (`test`, `doctor`, `scan`, `graph`, …).

**`dev`** is almost always a **repo task name**, not a built-in verb.
**`test`** is usually the built-in test orchestrator unless `tasks.test`
overrides it. Prefer `effigy <selector>` over raw `cargo` / `npm` /
`docker compose` when Effigy covers the path.

## Footguns (read first)

- **Never modify `.github/workflows/`** without explicit human approval.
- **Never bypass release gates.** Fix the underlying issue.
- **Never re-tag a failed release.** Fix lands in next PATCH.
- **Never run release mutations** (`release prepare/execute`) without explicit human ask.
- **Don't add `package.json` scripts** that re-export Effigy tasks.
- **Don't add `--repo .`** when already inside the target repo.

Full rationale: `references/footguns.md`.

## Agent routing

Do not run `doctor`, `tasks`, and `test --plan` as an automatic entry ritual.
Pick the first Effigy command that matches the job.

| Job | Use when | First command |
|-----|----------|---------------|
| Understand code | Ownership, behavior, implementation, impact | `effigy graph explore "<question>" --json` |
| Find runnable selectors | You need repo tasks or QA surfaces | `effigy tasks` |
| Inspect test shape | You need to know what `effigy test` will actually do | `effigy test --plan` |
| Diagnose routing or repo health | Selector resolution is unclear, or health/drift is the task | `effigy doctor` |
| Execute work | A task or built-in already covers the operation | `effigy <selector>` |
| Narrow validation | You changed code and want likely tests/files first | `git diff --name-only | effigy graph affected --stdin --json` |
| Parse results | Another tool/agent will consume the output | `effigy --json <command>` |

If `graph status --json` reports `refresh-recommended`, `degraded`, or
`missing-index`, run `effigy graph index --json` before trusting
explore/affected. Use `rg` for exact tokens and final pre-edit proof.

Details: `references/agent-operating-loop.md`, `references/graph-assist.md`.

## Routing rules

- use `graph` first for code-understanding questions, not for every task
- use `tasks` when you need selector inventory, not when you already know the task
- use `doctor` when routing is unclear or repo health is itself the task
- use `test --plan` when test execution shape matters, not as a greeting
- use selectors and built-ins for real execution work
- use `--json` whenever another agent step will consume the result

Good graph-first question shapes:

- `where are redirect responses handled`
- `where are config migrations validated before apply`
- `where does shell exit cleanup prompt run`

Stay with `rg` first when the job is:

- exact token lookup
- missing-symbol proof
- confirming the final pre-edit call site or string literal

Full built-in lookup: `references/built-in-surfaces.md`.

## Common workflows

| Goal | Command |
|------|---------|
| Orient in unfamiliar code | `effigy graph explore "<question>" --json` |
| Run tests | `effigy test` |
| Inspect test plan | `effigy test --plan` |
| Bring local dev up | `effigy container up` then `effigy dev` |
| Fast pre-push check | `effigy qa:ci:fast` (if defined) |
| Full local QA | `effigy qa` or `effigy qa:ci:local` |
| Repo health scan | `effigy doctor --verbose` |
| Scaffold manifest | `effigy init` then `effigy tasks migrate` |
| Check repo setup | `effigy init --check --json` or `effigy init --checklist --json` |
| Apply repo setup | `effigy init` or `effigy init --apply --json` |
| Read-only release check | `effigy release gates` |

Details: `references/workflow-shortcuts.md`.

## Selector routing (60 seconds)

1. **Alias prefix** (`qa:` → `qa:ci:fast`)
2. **Path prefix** (`api/test` → api catalog)
3. **CWD-nearest** workspace
4. **Shallowest** match if still ambiguous

Ambiguous? `effigy doctor <selector> <args...>` — then **stop and ask** if still unclear.

Details: `references/selector-routing.md`.

## JSON envelope

```bash
effigy --json <command>
```

Returns `effigy.command.v1` with command payload in `result` (or `error.details`).

**Exception:** `effigy graph watch --json` streams `effigy.graph.watch.event.v1`
lines — not the one-shot envelope.

```bash
effigy --json tasks | jq -r '.result.payload.tasks[].name'
```

Details: `references/json-envelope.md`.

## When to stop and ask

- Selector still ambiguous after `effigy doctor <selector> <args...>`
- `effigy doctor` structural FAIL on a task where repo health or routing matters
- User did not explicitly request release prepare/execute
- Change needs `.github/workflows/` edits
- Someone suggests skipping a release gate

## Specialized surfaces

Read these only when the repo or task needs them.

**Secrets** — `effigy secrets init`, `set`, `import`, `list`, `doctor` when
`[secrets]` is declared. Guide: `docs/guides/075-secrets-and-vault-guide.md`.

**Bundles** — `[bundle].base` with `path` / `git` / `oci`; `effigy bundle inspect`,
`effigy bundle sync`. Guide: `docs/guides/065-external-bundle-adoption.md`.

**Config shapes** — `[tasks]`, `[systems]`, `[containers]`, `[bootstrap]`,
`[release]`, `[bundle]`, `[secrets]`, `[state]`, `[deploy]`.
Details: `references/config-shapes.md`.

**State stacks** — `effigy state plan`, `apply`, `capture`.
Guide: `docs/guides/073-state-stack-guide.md`.

**Deployment** — `effigy deploy plan`, `apply`, `status` (human-gated apply).
Guide: `docs/guides/074-deployment-guide.md`.

**Release** — never mutate without explicit human instruction.
Sequence: `references/release-protocol.md`.

## Deeper docs

| Topic | Guide |
|-------|-------|
| Agent + graph workflow | `docs/guides/076-code-graph-and-agent-workflows.md` |
| Agent adoption | `docs/guides/047-agent-and-cross-repo-adoption.md` |
| Rhai script steps | `docs/guides/061-rhai-script-steps-guide.md` |
| Rhai host surface audit | `docs/guides/068-rhai-host-surface-audit.md` |
| Task routing | `docs/guides/016-task-routing-precedence.md` |
| JSON contracts | `docs/guides/017-json-output-contracts.md` |
| Quick start | `docs/guides/021-quick-start-and-command-cookbook.md` |
| Command reference | `docs/guides/025-command-reference-matrix.md` |
| Distribution evidence | `docs/guides/062-distribution-system-guide.md` |
| Containers / dev | `docs/guides/063-container-system-guide.md` |
| Release | `docs/guides/051-release-orchestration.md` |
| Built-in lookup | `references/built-in-surfaces.md` |
