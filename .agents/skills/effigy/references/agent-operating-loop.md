# Agent Operating Loop

Canonical sequence for agents in any repo with `effigy.toml`. Run phases in
order; skip a phase only when the user's request already satisfied it. This is
the default operating loop, not a claim that `graph` supersedes every other
Effigy surface.

## Phase 1 — Discover the surface (always)

```bash
effigy doctor
effigy tasks
effigy test --plan
```

Machine-readable inventory:

```bash
effigy --json tasks
effigy --json doctor
```

If routing is unclear:

```bash
effigy doctor <selector> <args...>
```

Details: `first-five-commands.md` (same commands, more expected-output notes).

## Phase 2 — Map codebase context (before broad scanning)

Do this when you need to find owners, trace behavior, or orient in an
unfamiliar tree — **before** spraying `rg` or opening many files.

```bash
effigy graph status --json
effigy graph index --json          # only when stale_paths is non-empty
effigy graph explore "<question>" --max-files 6 --max-bytes 12288 --json
```

Details: `graph-assist.md`.

Do not force this phase onto unrelated tasks. If the job is clearly execution,
deployment, state orchestration, docs validation, or release inspection, use
the matching built-in instead of inserting `graph` ritualistically.

## Phase 3 — Do the work

```bash
effigy <selector>                  # manifest task or built-in
effigy test                        # or effigy test --plan first
```

Prefer Effigy over raw `cargo` / `npm` / `docker compose` when a task or
built-in covers the path.

## Phase 4 — Narrow validation after edits

```bash
git diff --name-only | effigy graph affected --stdin --json
effigy test                        # or a repo-specific qa:* task
```

Use `graph affected` to pick a smaller target; it is not exhaustive proof.

## Phase 5 — Repo health (when drift matters)

```bash
effigy doctor --verbose            # includes enabled scan checks
effigy scan god-files --json       # individual scanners also available
```

## Phase not in the default loop

Use only when the repo or user needs them:

| Need | Surface |
|------|---------|
| Local stack | `effigy container up`, `effigy dev` (repo task) |
| Cross-repo clone | `effigy bootstrap <git-url>` |
| Secrets | `effigy secrets doctor`, `effigy secrets list` |
| State / deploy | `effigy state plan`, `effigy deploy plan` |
| Release cut | human-gated; see `release-protocol.md` |

## Source checkout fallback

When `effigy` is not on PATH inside the Effigy repo itself:

```bash
cargo run --bin effigy -- <command>
```

Outside that repo, install the binary per the project README or use
`effigy bootstrap`.
