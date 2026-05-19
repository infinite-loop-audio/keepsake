# JSON Envelope

Every Effigy command run with `--json` returns a single line of JSON in the
`effigy.command.v1` envelope shape.

## Envelope shape

```json
{
  "schema": "effigy.command.v1",
  "command": "tasks",
  "result": {
    "payload": { ... }
  },
  "error": null,
  "diagnostics": [ ... ]
}
```

On error:

```json
{
  "schema": "effigy.command.v1",
  "command": "tasks",
  "result": null,
  "error": {
    "code": "INVALID_MANIFEST",
    "message": "...",
    "details": { ... }
  },
  "diagnostics": [ ... ]
}
```

Key invariants:

- `schema` is always `effigy.command.v1`.
- Either `result` or `error` is non-null, never both.
- `result.payload` carries the command-specific data.
- `error.details` is structured (object), not a string.

## Payload schemas worth parsing

These commands produce stable JSON payloads suitable for agents to consume:

| Command | Payload contains |
|---------|------------------|
| `effigy --json tasks` | array of tasks with name, source catalog, kind |
| `effigy --json doctor` | check results with status, message, fix hint |
| `effigy --json doctor <selector> <args...>` | routing decision tree |
| `effigy --json test --plan` | resolved test plan tree |
| `effigy --json completion candidates` | shell completion candidates |
| `effigy --json config` | merged config tree |
| `effigy --json release status` | release gate states |
| `effigy --json graph status` | index freshness, stale paths, counts |
| `effigy --json graph explore "<q>"` | primary owners, excerpts, relations, guidance |
| `effigy --json graph affected` | affected files, likely tests/tasks after edits |
| `effigy --json graph context "<q>"` | ranked context items (lower-level than explore) |

**Streaming exception:** `effigy graph watch --json` emits newline-delimited
`effigy.graph.watch.event.v1` events — not wrapped in `effigy.command.v1`.

## Worked `jq` examples

**Pull all task names:**

```bash
effigy --json tasks | jq -r '.result.payload.tasks[].name'
```

**Find all failing doctor checks:**

```bash
effigy --json doctor \
  | jq -r '.result.payload.checks[] | select(.status == "FAIL") | .id + ": " + .message'
```

**Extract release gate states:**

```bash
effigy --json release status --check-gates \
  | jq -r '.result.payload.gates[] | "\(.name): \(.status)"'
```

**Check graph freshness before explore:**

```bash
effigy --json graph status | jq '.result.payload.stale_paths'
```

**Detect error envelope:**

```bash
output=$(effigy --json doctor)
if [ "$(echo "$output" | jq -r '.error')" != "null" ]; then
  echo "$output" | jq -r '.error.code + ": " + .error.message' >&2
  exit 1
fi
```

## Streaming vs single-shot

Most commands emit a **single envelope** as one line of JSON. Long-running
commands (`effigy run`, `effigy dev`) may emit multiple `effigy.event.v1`
lines followed by a terminal `effigy.command.v1`. Filter on `schema` to
distinguish.

## Full spec

`docs/guides/017-json-output-contracts.md` in the Effigy repo defines:

- complete envelope schema
- per-command payload schemas with field-level docs
- diagnostic levels and structured error codes
- versioning policy for envelope and payload schemas
