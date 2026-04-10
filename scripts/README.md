# Scripts

Use Effigy from the repo root for the default maintenance loop:

```bash
effigy tasks
effigy doctor
effigy qa
```

## Runtime Policy

- prefer `effigy` when it already covers the operation
- when repo-owned script logic is still needed, default to `TypeScript` run
  with `bun`
- use `bash` only for thin glue or compatibility boundaries
- use `python` or another runtime only with a concrete technical reason

## Working Rule

Keep custom scripts small, readable, and clearly subordinate to the Effigy
task surface rather than growing a parallel workflow system.

## Current Scripts

No custom scripts yet. Build tooling (CMake / Ninja) will be added here once
the project has a source tree.
