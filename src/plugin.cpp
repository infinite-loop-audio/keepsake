//
// Keepsake CLAP plugin — subprocess-bridged VST2 plugin instance.
// Each instance manages its own keepsake-bridge subprocess.
//

#include "plugin_internal.h"

// --- Helpers ---

bool send_and_wait(KeepsakePlugin *kp, uint32_t opcode,
                    const void *payload,
                    uint32_t size,
                    std::vector<uint8_t> *ok_payload) {
    if (kp->crashed || !kp->bridge) return false;
    std::lock_guard<std::mutex> lock(kp->ipc_mutex);
    if (!ipc_write_instance_msg(kp->bridge->proc.pipe_to, opcode,
                                 kp->instance_id, payload, size)) {
        kp->crashed = true;
        return false;
    }
    uint32_t resp_op;
    std::vector<uint8_t> resp_payload;
    if (!ipc_read_msg(kp->bridge->proc.pipe_from, resp_op, resp_payload, 3000)) {
        kp->crashed = true;
        return false;
    }
    if (resp_op == IPC_OP_ERROR) {
        std::string msg(resp_payload.begin(), resp_payload.end());
        fprintf(stderr, "keepsake: bridge error: %s\n", msg.c_str());
        return false;
    }
    if (resp_op != IPC_OP_OK) {
        fprintf(stderr, "keepsake: unexpected response 0x%02X\n", resp_op);
        return false;
    }
    if (ok_payload) *ok_payload = std::move(resp_payload);
    return true;
}

// Bridge pool pointer — set during factory init
static BridgePool *s_pool = nullptr;

void keepsake_plugin_set_pool(BridgePool *pool) { s_pool = pool; }

// Output silence to all output buffers.
static void output_silence(const clap_process_t *process) {
    for (uint32_t i = 0; i < process->audio_outputs_count; i++) {
        for (uint32_t ch = 0; ch < process->audio_outputs[i].channel_count; ch++) {
            if (process->audio_outputs[i].data32 && process->audio_outputs[i].data32[ch]) {
                memset(process->audio_outputs[i].data32[ch], 0,
                       process->frames_count * sizeof(float));
            }
        }
    }
}

// --- CLAP plugin callbacks ---

static bool plugin_init(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);

    // Acquire bridge process from pool
    kp->bridge = s_pool->acquire(
        kp->bridge_binary, kp->vst2_path, kp->format, kp->isolation);
    if (!kp->bridge) return false;

    // Send INIT with [format][path] — instance_id=0 for new instance
    std::vector<uint8_t> init_payload(4 + kp->vst2_path.size());
    memcpy(init_payload.data(), &kp->format, 4);
    memcpy(init_payload.data() + 4, kp->vst2_path.data(), kp->vst2_path.size());

    // INIT with instance_id=0 (bridge assigns a real one)
    if (!ipc_write_instance_msg(kp->bridge->proc.pipe_to, IPC_OP_INIT, 0,
                                 init_payload.data(),
                                 static_cast<uint32_t>(init_payload.size()))) {
        s_pool->release(kp->bridge); kp->bridge = nullptr;
        return false;
    }

    uint32_t resp_op;
    std::vector<uint8_t> ok_data;
    if (!ipc_read_msg(kp->bridge->proc.pipe_from, resp_op, ok_data, 10000) ||
        resp_op != IPC_OP_OK) {
        s_pool->release(kp->bridge); kp->bridge = nullptr;
        return false;
    }

    // Parse response: [instance_id][IpcPluginInfo][strings...]
    if (ok_data.size() >= 4 + sizeof(IpcPluginInfo)) {
        memcpy(&kp->instance_id, ok_data.data(), 4);
        IpcPluginInfo pi;
        memcpy(&pi, ok_data.data() + 4, sizeof(pi));
        kp->num_inputs = pi.num_inputs;
        kp->num_outputs = pi.num_outputs;
        kp->num_params = pi.num_params;
    }

    kp->bridge_ok = true;

    // Read editor flag from plugin info (already in ok_data)
    if (ok_data.size() >= 4 + sizeof(IpcPluginInfo)) {
        IpcPluginInfo pi2;
        memcpy(&pi2, ok_data.data() + 4, sizeof(pi2));
        kp->has_editor = (pi2.flags & 1) != 0;
        fprintf(stderr, "keepsake: init OK — in=%d out=%d params=%d editor=%d\n",
                kp->num_inputs, kp->num_outputs, kp->num_params, kp->has_editor);
    }

    // Parameter and editor rect queries are deferred to first access
    // to keep init() fast (REAPER times out on slow init).
    return true;
}

static void plugin_destroy(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    if (kp->bridge_ok && !kp->crashed && kp->bridge) {
        // Shutdown this instance (not the whole process)
        send_and_wait(kp, IPC_OP_SHUTDOWN);
    }
    if (kp->bridge && s_pool) {
        s_pool->release(kp->bridge);
        kp->bridge = nullptr;
    }
    if (kp->shm.ptr) platform_shm_close(kp->shm);
    delete kp;
}

static bool plugin_activate(const clap_plugin_t *plugin,
                              double sample_rate,
                              uint32_t min_frames,
                              uint32_t max_frames) {
    auto *kp = get(plugin);
    if (!kp->bridge_ok || kp->crashed) return false;
    (void)min_frames;

    kp->max_frames = max_frames;

    // Create shared memory for audio buffers
    char instance_id[32];
    snprintf(instance_id, sizeof(instance_id), "%p", static_cast<void *>(kp));
    std::string shm_name = platform_shm_name(instance_id);

    size_t shm_size = shm_total_size(kp->num_inputs, kp->num_outputs, max_frames);

    if (!platform_shm_create(kp->shm, shm_name, shm_size)) return false;

    // Initialize shared-memory mutex + condition variable
    shm_init_sync(shm_control(kp->shm.ptr));

    // Send SET_SHM: [name_len][name][shm_size]
    uint32_t name_len = static_cast<uint32_t>(shm_name.size());
    uint32_t shm_size32 = static_cast<uint32_t>(shm_size);
    std::vector<uint8_t> shm_payload(4 + name_len + 4);
    memcpy(shm_payload.data(), &name_len, 4);
    memcpy(shm_payload.data() + 4, shm_name.data(), name_len);
    memcpy(shm_payload.data() + 4 + name_len, &shm_size32, 4);

    if (!send_and_wait(kp, IPC_OP_SET_SHM,
                       shm_payload.data(),
                       static_cast<uint32_t>(shm_payload.size()))) {
        platform_shm_close(kp->shm);
        return false;
    }

    // Send ACTIVATE
    IpcActivatePayload ap = { sample_rate, max_frames };
    if (!send_and_wait(kp, IPC_OP_ACTIVATE, &ap, sizeof(ap))) {
        platform_shm_close(kp->shm);
        return false;
    }

    kp->active = true;
    return true;
}

static void plugin_deactivate(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    if (kp->active && !kp->crashed) {
        send_and_wait(kp, IPC_OP_DEACTIVATE);
    }
    if (kp->shm.ptr) platform_shm_close(kp->shm);
    kp->active = false;
    kp->max_frames = 0;
}

static bool plugin_start_processing(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    if (!kp->active || kp->crashed) return false;
    if (!send_and_wait(kp, IPC_OP_START_PROC)) return false;
    kp->processing = true;
    return true;
}

static void plugin_stop_processing(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    if (kp->processing && !kp->crashed) {
        send_and_wait(kp, IPC_OP_STOP_PROC);
    }
    kp->processing = false;
}

static void plugin_reset(const clap_plugin_t * /*plugin*/) {
    // No-op for now
}

static clap_process_status plugin_process(const clap_plugin_t *plugin,
                                            const clap_process_t *process) {
    auto *kp = get(plugin);

    if (kp->crashed || !kp->processing || !kp->shm.ptr) {
        output_silence(process);
        return kp->crashed ? CLAP_PROCESS_ERROR : CLAP_PROCESS_SLEEP;
    }

    uint32_t frames = process->frames_count;
    if (frames > kp->max_frames) frames = kp->max_frames;

    // --- Shared-memory audio hot path (zero syscalls) ---
    auto *ctrl = shm_control(kp->shm.ptr);

    // Copy input audio to shared memory
    for (int ch = 0; ch < kp->num_inputs; ch++) {
        float *dst = shm_audio_inputs(kp->shm.ptr, ch, kp->max_frames);
        if (process->audio_inputs_count > 0 &&
            ch < static_cast<int>(process->audio_inputs[0].channel_count) &&
            process->audio_inputs[0].data32 &&
            process->audio_inputs[0].data32[ch]) {
            memcpy(dst, process->audio_inputs[0].data32[ch],
                   frames * sizeof(float));
        } else {
            memset(dst, 0, frames * sizeof(float));
        }
    }

    // Write MIDI and param events into shared memory
    uint32_t midi_idx = 0;
    uint32_t param_idx = 0;
    if (process->in_events) {
        uint32_t ev_count = process->in_events->size(process->in_events);
        for (uint32_t i = 0; i < ev_count; i++) {
            auto *hdr = process->in_events->get(process->in_events, i);

            ShmMidiEvent *me = nullptr;
            if (hdr->type == CLAP_EVENT_MIDI && midi_idx < SHM_MAX_MIDI_EVENTS) {
                auto *midi = reinterpret_cast<const clap_event_midi_t *>(hdr);
                me = &ctrl->midi_events[midi_idx++];
                me->delta_frames = static_cast<int32_t>(hdr->time);
                me->data[0] = midi->data[0]; me->data[1] = midi->data[1];
                me->data[2] = midi->data[2]; me->data[3] = 0;

            } else if (hdr->type == CLAP_EVENT_NOTE_ON && midi_idx < SHM_MAX_MIDI_EVENTS) {
                auto *note = reinterpret_cast<const clap_event_note_t *>(hdr);
                me = &ctrl->midi_events[midi_idx++];
                me->delta_frames = static_cast<int32_t>(hdr->time);
                uint8_t vel = static_cast<uint8_t>(note->velocity * 127.0);
                if (vel == 0) vel = 1;
                me->data[0] = static_cast<uint8_t>(0x90 | (note->channel & 0x0F));
                me->data[1] = static_cast<uint8_t>(note->key & 0x7F);
                me->data[2] = vel; me->data[3] = 0;

            } else if (hdr->type == CLAP_EVENT_NOTE_OFF && midi_idx < SHM_MAX_MIDI_EVENTS) {
                auto *note = reinterpret_cast<const clap_event_note_t *>(hdr);
                me = &ctrl->midi_events[midi_idx++];
                me->delta_frames = static_cast<int32_t>(hdr->time);
                me->data[0] = static_cast<uint8_t>(0x80 | (note->channel & 0x0F));
                me->data[1] = static_cast<uint8_t>(note->key & 0x7F);
                me->data[2] = static_cast<uint8_t>(note->velocity * 127.0);
                me->data[3] = 0;

            } else if (hdr->type == CLAP_EVENT_PARAM_VALUE && param_idx < 64) {
                auto *pv = reinterpret_cast<const clap_event_param_value_t *>(hdr);
                ctrl->params[param_idx].index = static_cast<uint32_t>(pv->param_id);
                ctrl->params[param_idx].value = static_cast<float>(pv->value);
                param_idx++;
            }
        }
    }

    // Set control fields and signal the bridge
    ctrl->num_frames = frames;
    ctrl->midi_count = midi_idx;
    ctrl->param_count = param_idx;

    // Signal the bridge via shared-memory condition variable.
    // No pipes, no kernel transitions — pure shared memory sync.
#ifndef _WIN32
    pthread_mutex_lock(&ctrl->mutex);
    ctrl->state = SHM_STATE_PROCESS_REQUESTED;
    pthread_cond_signal(&ctrl->cond);

    // Wait for bridge to complete processing
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 100000000; // 100ms timeout
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }

    while (ctrl->state != SHM_STATE_PROCESS_DONE) {
        int r = pthread_cond_timedwait(&ctrl->cond, &ctrl->mutex, &deadline);
        if (r != 0) break; // timeout or error
    }

    bool done = (ctrl->state == SHM_STATE_PROCESS_DONE);
    ctrl->state = SHM_STATE_IDLE;
    pthread_mutex_unlock(&ctrl->mutex);

    if (!done) {
        output_silence(process);
        return CLAP_PROCESS_CONTINUE;
    }
#endif

    // Copy output audio from shared memory
    for (int ch = 0; ch < kp->num_outputs; ch++) {
        float *src = shm_audio_outputs(kp->shm.ptr, kp->num_inputs,
                                        ch, kp->max_frames);
        if (process->audio_outputs_count > 0 &&
            ch < static_cast<int>(process->audio_outputs[0].channel_count) &&
            process->audio_outputs[0].data32 &&
            process->audio_outputs[0].data32[ch]) {
            memcpy(process->audio_outputs[0].data32[ch], src,
                   frames * sizeof(float));
        }
    }

    return CLAP_PROCESS_CONTINUE;
}

// --- Audio-ports extension ---

static uint32_t audio_ports_count(const clap_plugin_t *plugin, bool is_input) {
    auto *kp = get(plugin);
    if (is_input) return kp->num_inputs > 0 ? 1 : 0;
    return kp->num_outputs > 0 ? 1 : 0;
}

static bool audio_ports_get(const clap_plugin_t *plugin, uint32_t index,
                              bool is_input, clap_audio_port_info_t *info) {
    auto *kp = get(plugin);
    if (index != 0) return false;

    memset(info, 0, sizeof(*info));
    info->id = is_input ? 0 : 1;
    snprintf(info->name, sizeof(info->name), "%s",
             is_input ? "Audio Input" : "Audio Output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = static_cast<uint32_t>(
        is_input ? kp->num_inputs : kp->num_outputs);
    info->port_type = info->channel_count == 2 ? CLAP_PORT_STEREO
                                                : CLAP_PORT_MONO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

static const clap_plugin_audio_ports_t s_audio_ports = {
    .count = audio_ports_count,
    .get = audio_ports_get,
};

// Extensions defined in separate files
extern const clap_plugin_params_t keepsake_params_ext;
extern const clap_plugin_state_t keepsake_state_ext;
extern const clap_plugin_gui_t keepsake_gui_ext;

// (params, state, and GUI extensions moved to plugin_params.cpp, plugin_state.cpp, plugin_gui.cpp)

// --- Latency extension ---
// The IPC round-trip adds inherent latency. Report it so the host
// can compensate for timing.

static uint32_t plugin_latency_get(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    // Report one buffer of latency for the IPC round-trip
    return kp->max_frames;
}

static const clap_plugin_latency_t s_latency = {
    .get = plugin_latency_get,
};

// --- get_extension ---

static const void *plugin_get_extension(const clap_plugin_t *plugin,
                                          const char *id) {
    if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &s_audio_ports;
    if (strcmp(id, CLAP_EXT_PARAMS) == 0) return &keepsake_params_ext;
    if (strcmp(id, CLAP_EXT_STATE) == 0) return &keepsake_state_ext;
    if (strcmp(id, CLAP_EXT_LATENCY) == 0) return &s_latency;
    if (strcmp(id, CLAP_EXT_GUI) == 0 && get(plugin)->has_editor)
        return &keepsake_gui_ext;
    return nullptr;
}

static void plugin_on_main_thread(const clap_plugin_t *) {}

// --- Factory entry point ---

const clap_plugin_t *keepsake_plugin_create(
    const clap_host_t *host,
    const clap_plugin_descriptor_t *descriptor,
    const std::string &plugin_path,
    const std::string &bridge_binary,
    int32_t num_inputs,
    int32_t num_outputs,
    uint32_t format,
    BridgePool *pool,
    IsolationMode isolation)
{
    s_pool = pool;

    auto *kp = new KeepsakePlugin();
    kp->host = host;
    kp->descriptor = descriptor;
    kp->vst2_path = plugin_path;
    kp->bridge_binary = bridge_binary;
    kp->num_inputs = num_inputs;
    kp->num_outputs = num_outputs;
    kp->format = format;
    kp->isolation = isolation;

    kp->clap = {};
    kp->clap.desc = descriptor;
    kp->clap.plugin_data = kp;
    kp->clap.init = plugin_init;
    kp->clap.destroy = plugin_destroy;
    kp->clap.activate = plugin_activate;
    kp->clap.deactivate = plugin_deactivate;
    kp->clap.start_processing = plugin_start_processing;
    kp->clap.stop_processing = plugin_stop_processing;
    kp->clap.reset = plugin_reset;
    kp->clap.process = plugin_process;
    kp->clap.get_extension = plugin_get_extension;
    kp->clap.on_main_thread = plugin_on_main_thread;

    return &kp->clap;
}
