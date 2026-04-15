//
// keepsake-bridge runtime helpers — instance state and non-GUI IPC handlers.
//

#include "bridge_runtime.h"
#include "bridge_gui.h"
#include "debug_log.h"

#include <cstdio>
#include <cstring>

void handle_set_shm(PluginInstance *inst, const std::vector<uint8_t> &payload) {
    if (payload.size() < 8) {
        ipc_write_error(g_pipe_out, "SET_SHM payload too small");
        return;
    }
    uint32_t name_len;
    memcpy(&name_len, payload.data(), 4);
    if (payload.size() < 4 + name_len + 4) {
        ipc_write_error(g_pipe_out, "SET_SHM payload malformed");
        return;
    }
    std::string name(payload.data() + 4, payload.data() + 4 + name_len);
    uint32_t shm_size;
    memcpy(&shm_size, payload.data() + 4 + name_len, 4);
    keepsake_debug_log("bridge: SET_SHM begin instance=%u name=%s size=%u\n",
                       inst ? inst->id : 0, name.c_str(), shm_size);

    if (inst->shm.ptr) platform_shm_close(inst->shm);
    if (!platform_shm_open(inst->shm, name, shm_size)) {
        keepsake_debug_log("bridge: SET_SHM FAILED instance=%u name=%s\n",
                           inst ? inst->id : 0, name.c_str());
        ipc_write_error(g_pipe_out, "failed to open shared memory");
        return;
    }
#ifdef _WIN32
    if (inst->shm_request_event != INVALID_HANDLE_VALUE) {
        CloseHandle(inst->shm_request_event);
        inst->shm_request_event = INVALID_HANDLE_VALUE;
    }
    if (inst->shm_done_event != INVALID_HANDLE_VALUE) {
        CloseHandle(inst->shm_done_event);
        inst->shm_done_event = INVALID_HANDLE_VALUE;
    }
    const std::string request_name = shm_event_name(name, "-req");
    const std::string done_name = shm_event_name(name, "-done");
    inst->shm_request_event = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE,
                                         request_name.c_str());
    inst->shm_done_event = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE,
                                      done_name.c_str());
    keepsake_debug_log("bridge: SET_SHM events instance=%u request=%p done=%p name=%s\n",
                       inst ? inst->id : 0,
                       inst->shm_request_event,
                       inst->shm_done_event,
                       name.c_str());
#endif
    keepsake_debug_log("bridge: SET_SHM OK instance=%u\n", inst ? inst->id : 0);
    ipc_write_ok(g_pipe_out);
}

void handle_activate(PluginInstance *inst, const std::vector<uint8_t> &payload) {
    if (!inst->loader || payload.size() < sizeof(IpcActivatePayload)) {
        ipc_write_error(g_pipe_out, "ACTIVATE: not ready");
        return;
    }
    IpcActivatePayload ap;
    memcpy(&ap, payload.data(), sizeof(ap));
    keepsake_debug_log("bridge: ACTIVATE begin instance=%u sr=%.1f max=%u\n",
                       inst ? inst->id : 0, ap.sample_rate, ap.max_frames);
    inst->max_frames = ap.max_frames;
    inst->loader->activate(ap.sample_rate, ap.max_frames);
    inst->active = true;
    bridge_audio_start(inst);
    keepsake_debug_log("bridge: ACTIVATE OK instance=%u active=%d\n",
                       inst ? inst->id : 0, inst->active ? 1 : 0);
    ipc_write_ok(g_pipe_out);
}

void handle_process(PluginInstance *inst, const std::vector<uint8_t> &payload) {
    if (!inst->loader || !inst->active || !inst->shm.ptr) {
        ipc_write_error(g_pipe_out, "PROCESS: not ready");
        return;
    }
    IpcProcessPayload pp;
    memcpy(&pp, payload.data(), sizeof(pp));
    uint32_t frames = pp.num_frames;
    if (frames > inst->max_frames) frames = inst->max_frames;

    auto *base = static_cast<float *>(inst->shm.ptr);
    std::vector<float *> in_ptrs(static_cast<size_t>(inst->num_inputs));
    std::vector<float *> out_ptrs(static_cast<size_t>(inst->num_outputs));
    for (int i = 0; i < inst->num_inputs; i++) {
        in_ptrs[static_cast<size_t>(i)] = base + i * inst->max_frames;
    }
    for (int i = 0; i < inst->num_outputs; i++) {
        out_ptrs[static_cast<size_t>(i)] = base + (inst->num_inputs + i) * inst->max_frames;
    }

    inst->loader->process(
        inst->num_inputs > 0 ? in_ptrs.data() : nullptr,
        inst->num_inputs,
        inst->num_outputs > 0 ? out_ptrs.data() : nullptr,
        inst->num_outputs,
        frames);

    ipc_write_process_done(g_pipe_out);
}

void handle_set_param(PluginInstance *inst, const std::vector<uint8_t> &payload) {
    if (!inst->loader || payload.size() < sizeof(IpcSetParamPayload)) return;
    IpcSetParamPayload sp;
    memcpy(&sp, payload.data(), sizeof(sp));
    inst->loader->set_param(sp.index, sp.value);
}

void handle_midi_event(PluginInstance *inst, const std::vector<uint8_t> &payload) {
    if (!inst->loader || payload.size() < sizeof(IpcMidiEventPayload)) return;
    IpcMidiEventPayload mp;
    memcpy(&mp, payload.data(), sizeof(mp));
    inst->loader->send_midi(mp.delta_frames, mp.data);
}

void handle_get_param_info(PluginInstance *inst, const std::vector<uint8_t> &payload) {
    if (!inst->loader || payload.size() < 4) {
        ipc_write_error(g_pipe_out, "GET_PARAM_INFO: not ready");
        return;
    }
    uint32_t index;
    memcpy(&index, payload.data(), 4);
    IpcParamInfoResponse resp = {};
    if (inst->loader->get_param_info(index, resp)) {
        ipc_write_ok(g_pipe_out, &resp, sizeof(resp));
    } else {
        ipc_write_error(g_pipe_out, "GET_PARAM_INFO: index out of range");
    }
}

void handle_get_chunk(PluginInstance *inst) {
    if (!inst->loader) {
        ipc_write_error(g_pipe_out, "GET_CHUNK: not ready");
        return;
    }
    auto chunk = inst->loader->get_chunk();
    ipc_write_ok(g_pipe_out, chunk.empty() ? nullptr : chunk.data(),
                 static_cast<uint32_t>(chunk.size()));
}

void handle_set_chunk(PluginInstance *inst, const std::vector<uint8_t> &payload) {
    if (!inst->loader || payload.empty()) {
        ipc_write_error(g_pipe_out, "SET_CHUNK: not ready");
        return;
    }
    inst->loader->set_chunk(payload.data(), payload.size());
    ipc_write_ok(g_pipe_out);
}

void handle_editor_get_rect(PluginInstance *inst) {
    IpcEditorRect rect = {};
    int w = 0;
    int h = 0;
    if (inst->loader && gui_get_editor_rect(inst->loader, w, h)) {
        rect.width = w;
        rect.height = h;
    }
    ipc_write_ok(g_pipe_out, &rect, sizeof(rect));
}

void handle_deactivate(PluginInstance *inst) {
    if (inst->loader && inst->active) {
        keepsake_debug_log("bridge: DEACTIVATE begin instance=%u\n",
                           inst ? inst->id : 0);
        inst->active = false;
        bridge_audio_stop(inst);
        inst->loader->deactivate();
    }
    keepsake_debug_log("bridge: DEACTIVATE OK instance=%u\n",
                       inst ? inst->id : 0);
    ipc_write_ok(g_pipe_out);
}
