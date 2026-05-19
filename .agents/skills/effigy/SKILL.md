---
name: effigy
description: >
  Effigy task-runner skill for agents: discover repo tasks (doctor, tasks,
  test --plan), use graph for code-navigation questions, run work through
  effigy selectors, parse --json envelopes, and avoid release/CI footguns.
  Use when the user mentions effigy, effigy.toml, or needs tests, dev, QA,
  repo navigation, deployment, or validation in an Effigy-adopting repo.
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

## Agent operating loop

**Default sequence in any repo with `effigy.toml`:**

```bash
# 1. Discover surface
effigy doctor
effigy tasks
effigy test --plan

# 2. Map code (before broad rg / file reads)
effigy graph status --json
effigy graph explore "<task-shaped question>" --max-files 6 --max-bytes 12288 --json

# 3. Do work
effigy <selector>
effigy test

# 4. After edits — narrow what to validate
git diff --name-only | effigy graph affected --stdin --json
```

| Phase | Use when |
|-------|----------|
| **Discover** | Always on entry — health, task list, test plan |
| **Graph** | You need owners, flow, or context before editing |
| **Execute** | Running repo work — tasks, tests, `qa:*` aggregators |
| **Affected** | Choosing validation scope after local edits |
| **JSON** | Parsing output programmatically — `effigy --json <command>` |

If `graph status --json` has non-empty `stale_paths`, run `effigy graph index --json`
before trusting explore/affected. Use `rg` for exact tokens and final pre-edit proof.

Details: `references/agent-operating-loop.md`, `references/graph-assist.md`.

## Agent jobs

Prioritize by the job you are doing, not by one globally dominant feature.

| Job | Use when | First command |
|-----|----------|---------------|
| Discover the repo | You just arrived, routing is unclear, or the task surface is unknown | `effigy doctor` |
| Inventory tasks | You need runnable selectors or QA/release surfaces | `effigy tasks` |
| Inspect test shape | You need to know what test execution will actually do | `effigy test --plan` |
| Understand code | The question is code-navigation shaped: ownership, flow, implementation, impact | `effigy graph explore "<question>" --json` |
| Execute work | A repo task or built-in already covers the requested operation | `effigy <selector>` |
| Validate changes | You need tests, QA, or narrower post-edit proof | `effigy test` or `git diff --name-only | effigy graph affected --stdin --json` |
| Parse results | Another agent/tool needs stable machine-readable output | `effigy --json <command>` |
| Use specialized surfaces | The task is domain-specific: state, deploy, distribution, bundles, secrets, docs, contracts, containers | start with the matching built-in |

Rule of thumb:

- use `doctor` / `tasks` / `test --plan` as the default entry sequence
- use `graph` first for code-understanding questions, not for every task
- use selectors and built-ins for real execution work
- use `--json` whenever another agent step will consume the result

Built-ins worth knowing beyond the default loop: `init`, `watch`, `defer`,
`docs`, `contracts`, `bundle`, `artifact`, `demo`, `changelog`,
`distribution`.

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
| Check repo setup | `effigy init --check --json` |
| Apply repo setup | `effigy init` |
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
- `effigy doctor` structural FAIL
- User did not explicitly request release prepare/execute
- Change needs `.github/workflows/` edits
- Someone suggests skipping a release gate

## Specialized surfaces

Read these only when the repo or task needs them.

**Secrets** — `effigy secrets init`, `set`, `list`, `doctor` when `[secrets]`
is declared. Guide: `docs/guides/075-secrets-and-vault-guide.md`.

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
| Containers / dev | `docs/guides/063-container-system-guide.md` |
| Release | `docs/guides/051-release-orchestration.md` |
