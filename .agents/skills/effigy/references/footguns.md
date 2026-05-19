# Footguns

Hard rules. Each one exists because violating it has caused a real,
hard-to-recover-from incident.

## 1. Never modify `.github/workflows/` without explicit human approval

CI workflow files gate releases, distribution, and publishing. A silent edit
can break the release pipeline, leak secrets, or skip a required gate.

If a workflow change is genuinely needed, surface the proposal to the human
and let them apply it.

## 2. Never bypass release gates

Gates exist to catch regressions before they hit users. Bypassing means the
next consumer hits the bug instead of you.

When a gate fails:

- Read the gate output (`effigy release status --check-gates` shows them all).
- Fix the underlying cause — failing test, missing changelog entry, dirty
  working tree, version mismatch.
- Re-run the gate.

Never use `--skip-gates`, `--force`, env-var overrides, or hand-edit lock
files to make a gate pass.

## 3. Never re-tag a failed release

If `effigy release execute` fails partway, the tag may already exist on the
remote, or the manifest cache may have recorded the attempt. Re-tagging:

- Confuses consumers who already pulled the broken tag.
- Breaks Homebrew/distribution caches that pin to immutable tags.
- Corrupts the changelog → tag mapping.

The fix lands as the **next PATCH release**. Add a `Fixed` entry to
`CHANGELOG.md` describing what was wrong, then run the standard release flow
for the next version.

## 4. Never run release commands without explicit human ask

Release commands cause public side effects: tags, GitHub releases, Homebrew
bottle uploads, distribution manifest updates.

Commands that require explicit human instruction:

- `effigy release prepare --yes`
- `effigy release execute --yes`
- `effigy release verify-install`
- Anything that pushes tags, creates GitHub releases, or uploads artifacts.

Read-only release commands (safe to run unprompted for inspection):

- `effigy release simulate`
- `effigy release status --check-gates`
- `effigy release prepare --plan`
- `effigy release execute --plan`
- `effigy release gates`
- `effigy changelog extract --version X.Y.Z`

## 5. Don't add `package.json` scripts re-exporting Effigy tasks

Re-exporting an Effigy task as an npm script (`"test": "effigy test"`)
duplicates the surface and creates drift:

- Two ways to run tests means people will pick the wrong one.
- The npm script can rot when Effigy task names change.
- Consumers think the package is npm-native when it's actually Effigy-driven.

Run `effigy <task>` directly. Keep `package.json` scripts package-native
(things only npm/bun knows about — bundling, type-checking via `tsc`, etc.).

## 6. Don't add `--repo .` when running inside the repo

`--repo <PATH>` is for **intentionally targeting a different repo** from
elsewhere on disk. Adding `--repo .` when already inside the target repo:

- Adds noise without changing behavior.
- Suggests the user wanted to target a different repo, then mistyped.
- Breaks if the agent later runs the command from a parent directory.

Just run `effigy <task>` from inside the repo.

## 7. Bonus: don't reinvent QA chains

If a repo has `qa:ci:fast`, `qa:ci:local`, or `qa` aggregator tasks, run those
instead of building a new chain. Aggregators encode the project's pre-push
contract — bypassing them means missing a gate.
