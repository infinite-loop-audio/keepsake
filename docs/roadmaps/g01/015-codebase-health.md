# G01.015 — Codebase Health

Status: ready
Owner: Infinite Loop Audio
Updated: 2026-04-11
Auto-continuation: allowed within g01

## Scope

Address accumulated technical debt: split god-files, resolve the license
question, and improve code organisation now that the architecture is
stable.

## Steps

### 1. Split large source files

`effigy doctor` reports god-files:
- `plugin.cpp` (579+ lines) — split into:
  - `plugin.cpp` — core lifecycle (init, destroy, activate, process)
  - `plugin_params.cpp` — params extension
  - `plugin_state.cpp` — state extension
  - `plugin_gui.cpp` — GUI extension
- `factory.cpp` (472+ lines) — split into:
  - `factory.cpp` — core factory callbacks
  - `scanner.cpp` — directory scanning and format detection
  - `scanner_vst3.cpp` — VST3-specific bridge scanning

### 2. License resolution

The VeSTige header is GPL v2+. The project LICENSE says LGPL v2.1.
Options:
- Change the project license to GPL v2+ (matches VeSTige exactly)
- Keep LGPL v2.1 for Keepsake's own code and document that the combined
  binary is GPL v2+ due to VeSTige inclusion
- Investigate whether the VeSTige ABI definitions (struct layouts,
  constants) are copyrightable under Oracle v Google precedent

Research and decide. Update LICENSE and README accordingly.

### 3. Code review pass

- Remove TODO comments that are now addressed by milestones
- Ensure consistent error handling patterns
- Verify all platform #ifdefs are correct
- Add missing function documentation

## Evidence Requirements

- `effigy doctor` god-file warnings resolved
- LICENSE file reflects the actual license situation
- Clean build on macOS after refactoring
