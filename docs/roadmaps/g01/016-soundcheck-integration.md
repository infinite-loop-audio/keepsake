# G01.016 — Soundcheck Integration

Status: deferred
Owner: Infinite Loop Audio
Updated: 2026-04-11
Governing refs:
  - docs/contracts/006-process-isolation-policy.md

## Scope

Replace Keepsake's built-in config.toml isolation settings with queries
to Soundcheck's local HTTP API. When Soundcheck is running, Keepsake reads
plugin metadata and isolation config from it. When Soundcheck is not
available, Keepsake falls back to its own config.toml.

Deferred until Soundcheck has a working HTTP API (Soundcheck Layer 5).

## Steps

### 1. Soundcheck API client

Implement a lightweight HTTP client in the factory that queries
Soundcheck's localhost API:

- `GET /plugins/{id}/isolation` — per-plugin isolation mode
- `GET /plugins` — full plugin list (could supplement or replace
  Keepsake's own scanning)
- Timeout/fallback: if Soundcheck is not running (connection refused),
  fall back to config.toml

### 2. Config priority chain

1. Soundcheck API (if running)
2. config.toml overrides
3. Built-in defaults

### 3. Bidirectional metadata

Keepsake could feed scan results back to Soundcheck:
- `POST /plugins` — report discovered plugins that Soundcheck might
  not know about (e.g., x86_64-only plugins detected via Rosetta bridge)
- Crash history from scan robustness (g01.014)

## Evidence Requirements

- Keepsake reads isolation settings from Soundcheck API
- Fallback to config.toml works when Soundcheck is not running
