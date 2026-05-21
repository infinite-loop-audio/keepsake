# First Commands By Job

Do not treat these as a mandatory startup ritual. Pick the first command that
matches the job in front of you.

For the full agent sequence and the code-understanding lane, see
`agent-operating-loop.md`.

**Selector reminder:** most names in `effigy tasks` are **manifest tasks**.
Built-ins include `test`, `init`, `doctor`, `tasks`, `scan`, and a few others
(see `effigy --help`). A repo's **`dev`** is almost always a **task**, not a
special CLI verb.

## 1. `effigy doctor`

Health check + routing diagnostic. Reports manifest validity, missing
dependencies, orphan tasks, container readiness, and whether the repo's
selectors resolve cleanly.

Expected: a list of checks with PASS/WARN/FAIL. If FAIL: stop and read the
explanation; the manifest is structurally broken or a required dependency is
missing.

Useful flags:

- `effigy doctor --json` — machine-readable envelope.
- `effigy doctor <selector> <args...>` — show why a selector resolves where it
  does.

Use this when routing is unclear, the repo may be drifted, or selector
resolution itself is the task.

## 2. `effigy tasks`

List all tasks the repo defines or composes. Aggregator tasks
(`qa:ci:fast`, `qa`, etc.) appear with their underlying chain expanded.

Expected: a flat or grouped list like:

```
test
fmt:check
build:release
qa:ci:fast
  ↳ bash -lc 'if cargo nextest --version ...'
  ↳ cargo test --doc
  ↳ qa:released-surface
  ↳ qa:ci:json
```

If the list is empty, the repo doesn't define tasks — it may rely entirely on
built-ins (`test`, `init`, `doctor`, …) or only nested-catalog tasks you have
not opened yet.

Use this when you need runnable selectors, QA entrypoints, or task inventory.

## 3. `effigy test --plan`

Show the resolved test plan **without running it**. Tells you which test
runners will fire, in what order, with what selectors.

Expected output is a plan tree. If the repo has `tasks.test` it overrides
the built-in plan; otherwise built-in test detection runs (cargo-nextest with
fallback to cargo test, vitest, etc.).

For machine parsing: `effigy test --plan --json`.

Use this when you need to know what `effigy test` would actually do before you
run it.

## 4. `effigy config`

Show resolved config — merged `effigy.toml` + `config/tasks.toml` +
catalog imports + defaults. This is what Effigy actually sees, after
inheritance.

Expected: TOML or JSON dump. Useful when a task isn't behaving as expected —
the merged view shows whether an override is in place.

For machine parsing: `effigy --json config`.

Use this when merged config is the thing you need to inspect.

## 5. `effigy --json tasks`

Same as `effigy tasks` but as the canonical `effigy.command.v1` envelope.
Use this when you need to programmatically pick a task to run or feed
results into another tool.

```bash
effigy --json tasks | jq -r '.result.payload.tasks[].name'
```

Use this when another tool or agent step needs task inventory in a stable JSON
envelope.

## Optional next: Graph (before broad `rg` / file reads)

When the job is code understanding, use this directly. Do not require the
other commands first if the question is already code-location shaped:

```bash
effigy graph status --json
effigy graph explore "<task-shaped question>" --max-files 6 --max-bytes 12288 --json
```

Details: `graph-assist.md`.

## What to do with the results

- **The first command answered the job** → proceed with the work the user asked for.
- **Need code ownership or flow next** → switch to `graph`.
- **`doctor` reports FAIL** → fix the underlying issue or hand back to user.
  Don't paper over a structural error.
- **`tasks` is empty + user asked for a workflow** → the repo may use raw
  tooling. Check for `package.json`, `Cargo.toml`, etc., and ask the user
  which surface they prefer.
- **Selector ambiguous in `effigy doctor <selector> <args...>`** → ask the user for the intended
  scope rather than guessing.
