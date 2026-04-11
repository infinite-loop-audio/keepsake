# G01.005 — MIDI, Parameter Automation, and State

Status: complete
Owner: Infinite Loop Audio
Updated: 2026-04-10
Governing refs:
  - docs/contracts/004-ipc-bridge-protocol.md
  - docs/contracts/002-clap-factory-interface.md
Auto-continuation: allowed within g01

## Scope

Make bridged plugins fully usable for real music production: complete MIDI
support (note expressions, CC, pitch bend), parameter automation (expose VST2
parameters as CLAP params, bidirectional sync), and session state (save/restore
plugin state via CLAP state extension so projects recall correctly).

Without this milestone, bridged plugins can process audio but cannot be
played, automated, or saved in a project.

## Steps

### 1. Complete MIDI support

Extend the bridge to handle the full range of MIDI events from CLAP:

- CLAP note events (`CLAP_EVENT_NOTE_ON`, `CLAP_EVENT_NOTE_OFF`) → VST2
  MIDI note on/off
- CLAP MIDI events (`CLAP_EVENT_MIDI`) → VST2 MIDI (already basic)
- CC, pitch bend, aftertouch, program change
- MIDI output from plugin → CLAP output events (for MIDI effects)
- Delta frame timing preserved through the bridge

Acceptance:
- A synth plugin (e.g., Vital) produces sound when receiving MIDI notes
  from the CLAP host
- CC automation reaches the plugin correctly

### 2. Parameter automation

Expose VST2 parameters as CLAP parameters:

- Implement CLAP params extension (`clap_plugin_params_t`)
- Query VST2 parameter count, names, and current values via bridge
  (`effGetParamName`, `getParameter`)
- Report parameter info to the host (name, min/max, default)
- Forward CLAP parameter change events to bridge (`SET_PARAM`)
- Forward bridge parameter changes back to host (when plugin's GUI changes
  a value) via `clap_host_params_t->request_flush`
- Extend IPC protocol with `PARAM_CHANGED` bridge→host opcode for
  bidirectional sync

Acceptance:
- Host shows parameter list for bridged plugins
- Automating a parameter in the host changes the plugin's behavior
- Changing a parameter in the plugin's GUI reflects in the host

### 3. State save/restore

Implement CLAP state extension for session recall:

- Implement `clap_plugin_state_t` (save/load)
- Use VST2 `effGetChunk` / `effSetChunk` via bridge IPC
- Add `GET_CHUNK` and `SET_CHUNK` opcodes to the bridge protocol
- Handle both program chunks and bank chunks

Acceptance:
- Saving and reopening a host project restores the plugin to its exact
  prior state
- Works for both preset-based and chunk-based plugins

### 4. Evidence

- Record audio from a synth plugin playing MIDI through the bridge
- Demonstrate parameter automation round-trip
- Demonstrate state save/restore across project reload

## Stop Conditions

- Stop if VST2 parameter naming is too inconsistent to expose reliably
  (document limits rather than inventing workarounds)

## Evidence Requirements

- Synth plugin producing audio from MIDI input
- Parameter automation visible and functional in a CLAP host
- Project save/load preserving plugin state

## Next Task

After this milestone: GUI forwarding (005) or CI (007).
