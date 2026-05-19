# Workflow Shortcuts

Common command chains, ordered by frequency.

Pick the chain that matches the current job. `graph` is the right default for
code-understanding questions, but not for every Effigy interaction.

## Map code before scanning

```bash
effigy graph status --json
effigy graph index --json                    # when stale_paths is non-empty
effigy graph explore "<question>" --max-files 6 --max-bytes 12288 --json
git diff --name-only | effigy graph affected --stdin --json
```

See `graph-assist.md`.

## Run tests

```bash
effigy test                  # built-in test detection or tasks.test override
effigy test --plan           # show plan without running
effigy test --json           # JSON envelope
effigy test <selector>       # run tests in a specific workspace
```

The built-in `test` prefers `cargo-nextest` when available, falling back to
`cargo test`. If the repo defines `tasks.test`, that overrides.

## Bring local dev up

```bash
effigy container up          # start containers declared in catalog
effigy gateway status        # confirm gateway routing reachable
effigy dev                   # repo task (commonly named dev); not a built-in verb
```

To tear down:

```bash
effigy container down
```

For deeper container ops: `docs/guides/063-container-system-guide.md` and
`docs/guides/064-system-workspace-and-dev-contract.md`.

## Pre-push validation

```bash
effigy qa:ci:fast            # example aggregator (Effigy repo defines qa:*)
effigy qa:ci:local           # fuller mirror when this repo defines it
effigy qa                    # full QA when this repo defines it
```

These **`qa:*`** selectors exist only in repos that declare them (the Effigy
source tree is the reference shape). Use `effigy tasks` to see what the current
repo actually exposes.

Use `qa:ci:fast` when iterating; `qa:ci:local` before pushing when your repo
mirrors branch CI that way.

## Manifest scaffolding

```bash
effigy init                        # idempotent baseline repo setup
effigy init --check --json         # no-write setup report for agents/tools
effigy tasks migrate --apply       # import package.json scripts into [tasks]
```

Preview without writing: `effigy tasks migrate` (omit `--apply`).

## Repo scanners

```bash
effigy doctor --verbose              # health + enabled scan checks
effigy scan god-files --json
effigy scan attention-markers --json
```

## Changelog

```bash
effigy changelog extract --version X.Y.Z         # extract a release section
effigy changelog extract CHANGELOG.md --version X.Y.Z  # explicit file
effigy changelog --json extract --version X.Y.Z  # JSON envelope
```

## Release inspection (read-only)

```bash
effigy release simulate                # dry-run the release flow
effigy release status --check-gates    # show gate states
effigy release prepare --plan          # preview prepare step
effigy release execute --plan          # preview execute step
effigy release gates                   # list gates and current pass/fail
```

These are safe to run unprompted. Anything with `--yes` or that pushes a tag
is **not** safe to run unprompted — see `release-protocol.md` and
`footguns.md`.

## Doctor + explain

```bash
effigy doctor                       # health + routing diagnostic
effigy doctor <selector> --           # why does this selector resolve here
effigy doctor --json                # machine-readable envelope
```

## JSON for everything

Append `--json` (or prefix with `effigy --json <command>`) to get an
`effigy.command.v1` envelope from any command. See `json-envelope.md`.

## Bootstrap from outside the repo

When you don't have the binary on PATH yet:

```bash
effigy bootstrap git@github.com:inflatable-cookie/effigy.git
```

Or run from source:

```bash
cargo run --bin effigy -- <command>
```
