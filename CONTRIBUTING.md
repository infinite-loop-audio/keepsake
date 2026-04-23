# Contributing to Keepsake

Contributions are welcome. This document covers everything you need to know before opening a PR — the development workflow, the legal constraints, and what makes a contribution useful to the project.

---

## Before you start

**Open an issue first** for anything non-trivial. This prevents duplicate work and lets us agree on scope before you write code. Bug reports and small fixes can go straight to a PR.

What's most useful right now:

- Platform testing and bug reports (especially Windows and Linux)
- VST2 edge case coverage — parameter handling, MIDI, GUI lifecycle, unusual plugin behaviour
- Crash reproduction cases with specific legacy plugins
- Documentation gaps or corrections
- Build system improvements

---

## Legal — read this first

Keepsake has one hard legal constraint: **VST2 support uses [VeSTige](https://github.com/LMMS/lmms/blob/master/plugins/vst_base/vestige/aeffect.h) only.** The Steinberg VST2 SDK is not used, not referenced, and must not appear in this repository.

VeSTige is a clean-room reverse-engineered implementation of the VST2 ABI, originally written by Javier Serrano Polo. It has been part of LMMS for over 20 years under LGPL v2.1. Keepsake's licence matches this lineage.

If any implementation path requires using or referencing the Steinberg VST2 SDK, stop and say so — it cannot proceed. This is a legal constraint, not a preference.

**VST3:** The VST3 pluginterfaces (GPLv3) are permitted for VST3 hosting, but only inside the subprocess bridge. The licence boundary sits at the process/IPC edge. VST3 code must not bleed into the main plugin process.

**CLAP** remains the outer plugin format. Do not propose changes that would make VST3 or another format the outer surface.

---

## Development setup

Full build instructions for all three platforms (including Windows MSVC setup, Linux dependencies, and 32-bit bridge builds): **[docs/setup/building.md](docs/setup/building.md)**

Short version for macOS and Linux:

```sh
# Linux: sudo apt-get install -y cmake build-essential libx11-dev
cmake --preset default
cmake --build build
effigy doctor    # verify your environment is healthy
effigy qa        # full quality check
```

If `effigy doctor` reports problems, fix those before doing anything else. External dependencies (CLAP SDK, VST3 pluginterfaces) are fetched automatically by CMake on first configure.

---

## Development workflow

The core loop:

```sh
effigy tasks          # see what's available
effigy doctor         # health check
effigy qa             # build + test + validate
```

For validating real-plugin behaviour:

```sh
effigy demo list                # list demos
effigy demo:supported-proof     # run the primary validation suite (macOS + REAPER + VST2)
effigy demo run <demo-id>       # run a specific demo
```

Prefer `effigy <task>` over raw CMake or shell commands when there's a task that covers the operation. When there isn't one, raw tools are fine.

**Scripting:** if repo-owned script logic is needed beyond what Effigy handles, use TypeScript run with Bun. Bash is fine for thin glue. Avoid introducing new scripting runtimes without a concrete reason.

---

## Code conventions

The codebase is C/C++ (C++20, C11 minimum). A few things to follow:

**Platform abstractions:** Cross-platform code lives in `src/platform*.h`. When adding platform-specific behaviour, go through those abstractions rather than sprinkling `#ifdef` throughout the feature code. Look at how existing platform splits are structured before adding new ones.

**Bridge boundary:** The subprocess bridge (`src/bridge_*`) is a separate executable. Code in `src/plugin*.cpp` runs in the host process; code in `src/bridge_*.cpp` runs in the child process. Don't mix them. IPC between the two goes through `src/ipc.h` — extend the protocol there if you need new message types.

**Format loaders:** Each legacy format has its own loader:
- VST2: `src/vst2_loader.*` — VeSTige only
- VST3: `src/bridge_loader_vst3.*` — subprocess-isolated
- AU v2: `src/bridge_loader_au.mm` — macOS, subprocess-isolated

Keep format-specific code in these files rather than leaking it into shared paths.

**No external dependencies** beyond what's already in the tree. Adding a new dependency needs a clear justification and a licence check.

**Formatting:** match the existing style. The codebase uses 4-space indentation, `snake_case` for functions and variables, and `PascalCase` for types.

---

## Testing your changes

Before opening a PR:

1. **`effigy qa` passes** — this is the minimum bar
2. **Test on the primary lane** if at all possible: macOS + REAPER + VST2. If you don't have access to macOS, say so in the PR and describe how you validated on your platform.
3. **Run a demo proof** if your change touches scanning, loading, or audio: `effigy demo:supported-proof`
4. **For GUI changes on macOS:** test with at least one real plugin (Serum, APC, or a plugin with a complex editor)

If you're adding a new capability or fixing a crash, describe what you tested and what you observed — not just that tests pass.

---

## Submitting a pull request

- **Reference the issue** your PR addresses
- **Keep PRs focused** — one logical change per PR is easier to review and revert if needed
- **Describe what changed and why** in the PR body, not just what files were touched
- **Be explicit about limitations** — if something is untested on Windows, say so
- **Don't broaden support claims** in documentation without evidence to back them. If the claim isn't proven, mark it experimental or don't add it yet

PRs for experimental platforms (Windows, Linux) that include real-host validation evidence carry significantly more weight than code-only submissions.

---

## Architecture and planning context

If you're doing anything beyond a targeted bug fix, read these first:

- [`docs/architecture/system-architecture.md`](docs/architecture/system-architecture.md) — component layout and seams
- [`docs/architecture/product-guardrails.md`](docs/architecture/product-guardrails.md) — things Keepsake deliberately doesn't do
- [`docs/contracts/002-clap-factory-interface.md`](docs/contracts/002-clap-factory-interface.md) — the CLAP factory contract
- [`docs/contracts/004-ipc-bridge-protocol.md`](docs/contracts/004-ipc-bridge-protocol.md) — IPC message protocol
- [`docs/contracts/006-process-isolation-policy.md`](docs/contracts/006-process-isolation-policy.md) — isolation modes and crash policy

These are the authoritative references for the project's design decisions. If your change conflicts with something in them, note that in your PR — it may be a design tradeoff worth discussing rather than something to paper over.
