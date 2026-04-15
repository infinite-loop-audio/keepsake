//
// Keepsake CLAP plugin — subprocess-bridged VST2 plugin instance.
// Each instance manages its own keepsake-bridge subprocess.
//

#include "plugin_internal.h"
#include "debug_log.h"

static const char *ipc_opcode_name(uint32_t opcode) {
    switch (opcode) {
    case IPC_OP_INIT: return "INIT";
    case IPC_OP_SET_SHM: return "SET_SHM";
    case IPC_OP_ACTIVATE: return "ACTIVATE";
    case IPC_OP_PROCESS: return "PROCESS";
    case IPC_OP_SET_PARAM: return "SET_PARAM";
    case IPC_OP_STOP_PROC: return "STOP_PROC";
    case IPC_OP_START_PROC: return "START_PROC";
    case IPC_OP_DEACTIVATE: return "DEACTIVATE";
    case IPC_OP_SHUTDOWN: return "SHUTDOWN";
    case IPC_OP_MIDI_EVENT: return "MIDI_EVENT";
    case IPC_OP_GET_PARAM_INFO: return "GET_PARAM_INFO";
    case IPC_OP_GET_CHUNK: return "GET_CHUNK";
    case IPC_OP_SET_CHUNK: return "SET_CHUNK";
    case IPC_OP_EDITOR_OPEN: return "EDITOR_OPEN";
    case IPC_OP_EDITOR_CLOSE: return "EDITOR_CLOSE";
    case IPC_OP_EDITOR_GET_RECT: return "EDITOR_GET_RECT";
    case IPC_OP_EDITOR_SET_PARENT: return "EDITOR_SET_PARENT";
    case IPC_OP_EDITOR_MOUSE: return "EDITOR_MOUSE";
    case IPC_OP_EDITOR_KEY: return "EDITOR_KEY";
    default: return "UNKNOWN";
    }
}

bool send_and_wait(KeepsakePlugin *kp, uint32_t opcode,
                    const void *payload,
                    uint32_t size,
                    std::vector<uint8_t> *ok_payload,
                    int timeout_ms) {
    if (kp->crashed || !kp->bridge) return false;
    keepsake_debug_log("keepsake: send_and_wait begin opcode=%s instance=%u timeout=%d\n",
                       ipc_opcode_name(opcode), kp->instance_id, timeout_ms);
    std::lock_guard<std::mutex> lock(kp->ipc_mutex);
    bool ok = send_and_wait_bridge(kp->bridge, kp->instance_id, opcode,
                                   payload, size, ok_payload, timeout_ms);
    if (!ok) {
        const bool alive = platform_process_alive(kp->bridge->proc);
        fprintf(stderr,
                "keepsake: %s failed after %dms (bridge pid=%d alive=%d instance=%u)\n",
                ipc_opcode_name(opcode), timeout_ms,
                static_cast<int>(kp->bridge->proc.pid), alive ? 1 : 0,
                kp->instance_id);
        switch (opcode) {
        case IPC_OP_EDITOR_OPEN:
        case IPC_OP_EDITOR_CLOSE:
        case IPC_OP_EDITOR_GET_RECT:
        case IPC_OP_EDITOR_SET_PARENT:
        case IPC_OP_EDITOR_MOUSE:
        case IPC_OP_EDITOR_KEY:
            break;
        default:
            kp->crashed = true;
            break;
        }
    } else {
        keepsake_debug_log("keepsake: send_and_wait OK opcode=%s instance=%u\n",
                           ipc_opcode_name(opcode), kp->instance_id);
    }
    return ok;
}

static BridgePool *s_pool = nullptr;

void keepsake_plugin_set_pool(BridgePool *pool) { s_pool = pool; }

void queue_async_activation(KeepsakePlugin *kp,
                            const std::string &shm_name,
                            uint32_t shm_size,
                            double sample_rate,
                            uint32_t max_frames,
                            bool want_start_processing) {
    auto state = kp->async_init;
    if (!state) return;
    std::lock_guard<std::mutex> lock(state->mutex);
    state->shm_name = shm_name;
    state->shm_size = shm_size;
    state->sample_rate = sample_rate;
    state->max_frames = max_frames;
    state->want_activate = true;
    state->want_start_processing = want_start_processing;
}

void clear_async_queue(KeepsakePlugin *kp, bool clear_activate) {
    auto state = kp->async_init;
    if (!state) return;
    std::lock_guard<std::mutex> lock(state->mutex);
    if (clear_activate) state->want_activate = false;
    state->want_start_processing = false;
}

BridgePool *keepsake_plugin_pool() {
    return s_pool;
}

void abandon_bridge(KeepsakePlugin *kp, const char *reason) {
    if (!kp) return;

    keepsake_debug_log("keepsake: abandoning bridge instance=%u reason=%s bridge_ok=%d crashed=%d\n",
                       kp->instance_id,
                       reason ? reason : "(unspecified)",
                       kp->bridge_ok ? 1 : 0,
                       kp->crashed ? 1 : 0);

    BridgeProcess *bridge = kp->bridge;
    kp->bridge = nullptr;
    kp->bridge_ok = false;
    kp->crashed = true;
    keepsake_gui_session_mark_closed(kp);
    kp->gui_embed_failed = true;
    kp->gui_is_floating = false;

    if (bridge && keepsake_plugin_pool()) {
        keepsake_plugin_pool()->abandon(bridge);
    }
}
