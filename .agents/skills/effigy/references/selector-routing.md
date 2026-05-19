# Selector Routing

A **selector** is the string the user types after `effigy` to identify a
task: `test`, `api/test`, `qa:ci:fast`, `web/build`. Effigy resolves it to
exactly one task in exactly one catalog.

Routing precedence, top to bottom (first match wins):

1. **Alias prefix** — colon-prefixed names (`qa:`, `release:`,
   `prepush:`) match defined aliases first.
2. **Path prefix** — slash-prefixed names (`api/test`, `web/build`) match
   the workspace at that path.
3. **CWD-nearest** — when the selector is bare (`test`), Effigy finds the
   workspace nearest to the current working directory and uses its task.
4. **Shallowest match** — when CWD doesn't disambiguate, the workspace
   closest to the repo root wins.

## Worked example

Repo layout:

```
.
├── effigy.toml
├── config/tasks.toml            # defines `test`, `qa:ci:fast`
├── api/
│   └── effigy.toml               # defines `test`
└── web/
    └── effigy.toml               # defines `test`
```

| User runs (CWD) | Resolves to |
|-----------------|-------------|
| `effigy test` (in repo root) | root `test` (shallowest) |
| `effigy test` (in `api/`) | `api/` `test` (CWD-nearest) |
| `effigy api/test` (anywhere) | `api/` `test` (path prefix) |
| `effigy qa:ci:fast` (anywhere) | root `qa:ci:fast` (alias prefix) |

## Disambiguation

When a selector resolves somewhere unexpected, run:

```bash
effigy doctor <selector> --
```

This shows the routing decision tree — which catalogs were considered, which
matched, and why one won.

## When to stop and ask

If `effigy doctor <selector> --` returns multiple candidates with no clear
winner, **stop and ask** the user which scope they meant. Don't guess.

Common ambiguity causes:

- Two workspaces define the same task name with no path prefix.
- A workspace was added recently and its `effigy.toml` isn't yet in the
  catalog cache (run `effigy doctor` to refresh).
- A task is defined as both an alias and a workspace task.

## Full spec

`docs/guides/016-task-routing-precedence.md` in the Effigy repo has the
complete spec including catalog inheritance, alias resolution edge cases,
and routing-related JSON envelopes.
