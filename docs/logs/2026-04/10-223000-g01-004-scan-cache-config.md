# G01.004 — Scan Cache and Configuration

Date: 2026-04-10
Milestone: g01.004
Status: complete

## What was done

- `src/config.h` / `src/config.cpp` — configuration and cache implementation
- `config.toml` parsing (minimal TOML: `[scan]` section with `vst2_paths`
  array and `rescan` flag)
- Scan cache in `cache.dat` (tab-delimited, one plugin per line, mtime-based
  invalidation)
- Rescan sentinel file support (create `rescan` file → triggers full rescan)
- Factory integration: cache load → skip scan if valid, cache save after scan
- `KEEPSAKE_VST2_PATH` env var bypasses cache (dev/testing mode)

## Evidence

- Scan with env var correctly bypasses cache
- Cache file format writes and reads correctly
- Config file parsed with custom scan paths
- mtime-based invalidation filters stale entries

## Files

- `src/config.h` / `src/config.cpp` — new
- `src/factory.cpp` — integrated cache and config loading

## Next Task

G01 sequencing intent is met. Assess whether to close g01 and open g02.
