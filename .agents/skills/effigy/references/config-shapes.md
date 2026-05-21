# Config Shapes

Realistic snippets for the config sections an agent is likely to author or
modify. Not a full reference — see `docs/guides/025-command-reference-matrix.md`
for the complete schema.

Effigy splits config across:

- `effigy.toml` — project-level (catalogs, systems, containers, release).
- `config/tasks.toml` — task definitions (kept separate so task
  changes don't churn the project manifest).

**Tasks vs built-ins:** `[tasks]` defines ordinary selectors such as **`dev`** or
**`qa:ci:fast`**. Built-in commands (`test`, `init`, `doctor`, …) come from Effigy
itself; see `effigy --help` for the list.

## `[tasks]`

Tasks can be shell strings, refs to other tasks, or Rhai scripts. Examples:

```toml
[tasks]
# Shell string
"fmt:check" = "cargo fmt --all -- --check"

# Aggregator: chain of task refs
"qa:ci:fast" = [
  "cargo test",
  "cargo test --doc",
  { task = "qa:released-surface" },
  { task = "qa:ci:json" },
]

# Rhai script
"db:seed" = { rhai = "scripts/seed-db.rhai" }

# Rhai deploy plan wrapper
"deploy:uat:plan" = { rhai = "scripts/deploy-uat-plan.rhai" }

# Rhai distribution validation wrapper
"release:artifacts:check" = { rhai = "scripts/check-release-artifacts.rhai" }

# Mixed chain
"bootstrap:local" = [
  { rhai = "scripts/build-local-bin.rhai" },
  { rhai = "scripts/install-local-bin-links.rhai" },
]

# Task with explicit run block (for richer config)
[tasks."smoke:release"]
run = { rhai = "scripts/check-release-smoke.rhai" }
```

Typical Rhai wrappers for typed deploy / distribution helpers:

```rhai
// scripts/deploy-uat-plan.rhai
let plan = deploy::plan(#{ env: "uat", write_report: true });
if !plan["ok"] { throw("deploy plan failed"); }
```

```rhai
// scripts/check-release-artifacts.rhai
let artifacts = distribution::validate_artifacts(#{
    artifacts_dir: "artifacts/release",
    expect_homebrew: true,
});
if !artifacts["ok"] { throw("artifact validation failed"); }
```

## `[systems.<name>]`

Systems group containers and workspaces. One system can have many workspaces.

```toml
[systems.release]
default_workspace = "linux"

[systems.release.workspaces.linux]
container = "linux-release"
```

## `[containers.<name>]`

Container definitions live in catalogs (often imported from a shared catalog
crate). Repo-local containers go here:

```toml
[containers.linux-release]
image = "ghcr.io/inflatable-cookie/effigy-linux-release:latest"
volumes = [
  { source = ".", target = "/workspace", mode = "rw" },
]
```

For Effigy-defined catalogs (`workspace-rust-bun`, `php-fpm`, `node`), import
from the workspace catalog crate rather than redefining.

## `[bootstrap]`

First-run setup steps, executed by `effigy bootstrap`:

```toml
[bootstrap]
run = [
  { task = "bootstrap deps sync" },
  { task = "doctor" },
]
start = "dev"                              # single selector
# start = ["container:up", "dev"]          # or array, sequential
# start = [{ task = "container:up" }, { task = "dev" }]   # table form
submodules = "recursive"
```

`start` accepts a scalar selector or an array. Array entries are either
bare selector strings or `{ task = "..." }` tables (mixed allowed).
Args travel inline in the selector string (`"dev --foo bar"`). Arrays
run in declaration order; the first failure aborts the chain.

## `[release]`

Release configuration: gates, manifest path, distribution targets.

```toml
[release]
manifest_path = "release/manifest.toml"
gates = [
  "fmt",
  "clippy",
  "test",
  "docs",
  "json-contracts",
  "released-surface",
]
```

The actual release manifest (`release/manifest.toml`) is separate and tracks
version, changelog cutoff, distribution channels.

## Catalog imports

Catalogs let you share container/system definitions across repos:

```toml
[catalog]
imports = [
  { crate = "effigy-containers", catalog = "workspace-rust-bun" },
]
```

After importing, reference the container by its catalog name:

```toml
[systems.dev.workspaces.main]
container = "workspace-rust-bun"
```
