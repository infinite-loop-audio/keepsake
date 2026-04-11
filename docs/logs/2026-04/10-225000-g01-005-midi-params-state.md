# G01.005 — MIDI, Parameter Automation, and State

Date: 2026-04-10
Milestone: g01.005
Status: complete

## What was done

### CLAP note events → MIDI (step 1)

- `CLAP_EVENT_NOTE_ON` → MIDI note-on (0x90 | channel, key, velocity*127)
- `CLAP_EVENT_NOTE_OFF` → MIDI note-off (0x80 | channel, key, velocity*127)
- `CLAP_EVENT_MIDI` → raw MIDI passthrough (existing, unchanged)
- Delta frame timing preserved through the bridge
- All event types forwarded before PROCESS command in the audio callback

### Parameter automation (step 2)

- Extended IPC protocol: `GET_PARAM_INFO` opcode (0x0B) — bridge queries
  effGetParamName, effGetParamLabel, getParameter and returns
  IpcParamInfoResponse
- All parameters queried and cached during plugin_init (one IPC round-trip
  per parameter)
- Implemented `clap_plugin_params_t`: count, get_info, get_value,
  value_to_text, text_to_value, flush
- VST2 params exposed as CLAP params with id = param index, range 0.0–1.0
- `CLAP_EVENT_PARAM_VALUE` events in process() and flush() forwarded to
  bridge via SET_PARAM

### State save/restore (step 3)

- Extended IPC protocol: `GET_CHUNK` (0x0C) and `SET_CHUNK` (0x0D) opcodes
- Bridge uses effGetChunk/effSetChunk for VST2 state serialization
- Implemented `clap_plugin_state_t`: save writes [size][chunk], load reads
  [size][chunk] and sends to bridge
- Handles plugins without chunk support gracefully (empty save)

## Files modified

- `src/ipc.h` — new opcodes, IpcParamInfoResponse struct
- `src/bridge_main.cpp` — handle_get_param_info, handle_get_chunk,
  handle_set_chunk
- `src/plugin.h` — CachedParamInfo struct, params vector in KeepsakePlugin
- `src/plugin.cpp` — params extension, state extension, note event
  conversion, param event forwarding

## Evidence

Build succeeds cleanly. All extensions registered in get_extension. The
params, state, and note event handling will be fully exercised when loaded in
a CLAP host (REAPER, Bitwig, etc.) — the clap-scan tool only exercises the
factory, not plugin instantiation.

## Next Task

Ship g01.006 (GUI forwarding) or g01.007 (CI).
