# Graph Assist

Use the local code graph when you need repo context **before** broad `rg` or
file reads. The graph is a deterministic index under `.effigy/graph/graph.db` —
a navigation map, not compiler-grade truth. It is the code-understanding lane
inside the wider Effigy workflow, not the universal front door.

Start with `effigy doctor`, `effigy tasks`, and `effigy test --plan` when the
repo surface is still unknown. Switch to graph when the question becomes code
ownership, flow, implementation, or changed-file impact.

## When to use it

| Situation | Command |
|-----------|---------|
| "Where is X implemented?" / task-shaped navigation | `graph explore` |
| "What should I run after these edits?" | `graph affected` |
| Index freshness / stale files | `graph status` |
| Lower-level ranked hits without explore assembly | `graph context` |
| Known term, unknown symbol id | `graph search` |
| Symbol id already known | `graph node`, `graph callers`, `graph callees` |
| Exact token / missing symbol proof | `rg` (not graph) |

## Standard sequence

```bash
effigy graph status --json
# if stale_paths is non-empty:
effigy graph index --json

effigy graph explore "<task-shaped question>" \
  --max-files 6 --max-bytes 12288 --json
```

After edits:

```bash
git diff --name-only | effigy graph affected --stdin --json
```

## Rules

- **`graph explore` first** for task-shaped questions. Trust excerpts for
  first-pass orientation; open files only when the excerpt is insufficient.
- **`graph index` is explicit.** Queries do not rebuild the index for you.
- **`graph affected` narrows validation** — it does not prove exhaustive test
  reachability.
- **`rg` stays mandatory** for exact tokens, missing symbols, and pre-edit proof.
- **Rebuild on corruption or unknown future schema:**

```bash
rm -rf .effigy/graph
effigy graph index --json
```

## Lower-level context packet

When you want ranked items without the assembled explore shape:

```bash
effigy graph context "<question>" --max-files 8 --max-bytes 4096 --json
```

Optional filters: `--language rust`, `--path src/runner`.

## Watch mode (long-running repos)

```bash
effigy graph watch --debounce-ms 1000 --json
```

`graph watch --json` streams newline-delimited `effigy.graph.watch.event.v1`
events. It does **not** use the one-shot `effigy.command.v1` envelope.

## JSON schemas

| Command | Schema |
|---------|--------|
| `graph status` | `effigy.graph.status.v1` |
| `graph explore` | `effigy.graph.explore.v1` |
| `graph affected` | `effigy.graph.affected.v1` |
| `graph context` | `effigy.graph.context.v1` |
| `graph watch --json` | `effigy.graph.watch.event.v1` (streaming) |

Full spec: `docs/guides/076-code-graph-and-agent-workflows.md`
