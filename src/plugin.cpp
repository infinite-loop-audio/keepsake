//
// Keepsake CLAP plugin — subprocess-bridged VST2 plugin instance.
// Each instance manages its own keepsake-bridge subprocess.
//

#include "plugin.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

// --- Helpers ---

static KeepsakePlugin *get(const clap_plugin_t *plugin) {
    return reinterpret_cast<KeepsakePlugin *>(plugin->plugin_data);
}

// Send a command and wait for OK/ERROR response.
// Returns true if OK received.
static bool send_and_wait(KeepsakePlugin *kp, uint32_t opcode,
                           const void *payload = nullptr,
                           uint32_t size = 0,
                           std::vector<uint8_t> *ok_payload = nullptr) {
    if (kp->crashed || !kp->bridge) return false;
    if (!ipc_write_instance_msg(kp->bridge->proc.pipe_to, opcode,
                                 kp->instance_id, payload, size)) {
        kp->crashed = true;
        return false;
    }
    uint32_t resp_op;
    std::vector<uint8_t> resp_payload;
    if (!ipc_read_msg(kp->bridge->proc.pipe_from, resp_op, resp_payload, 10000)) {
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

    // Query parameter info from bridge (cached for the plugin's lifetime)
    kp->params.resize(static_cast<size_t>(kp->num_params));
    for (int32_t i = 0; i < kp->num_params; i++) {
        uint32_t idx = static_cast<uint32_t>(i);
        std::vector<uint8_t> param_data;
        if (send_and_wait(kp, IPC_OP_GET_PARAM_INFO, &idx, sizeof(idx),
                          &param_data) &&
            param_data.size() >= sizeof(IpcParamInfoResponse)) {
            IpcParamInfoResponse resp;
            memcpy(&resp, param_data.data(), sizeof(resp));
            auto &cp = kp->params[static_cast<size_t>(i)];
            cp.index = resp.index;
            cp.default_value = resp.current_value;
            memcpy(cp.name, resp.name, sizeof(cp.name));
            memcpy(cp.label, resp.label, sizeof(cp.label));
        }
    }
    fprintf(stderr, "keepsake: cached %d parameter(s)\n", kp->num_params);

    // Check for editor support and query size
    if (ok_data.size() >= sizeof(IpcPluginInfo)) {
        IpcPluginInfo pi2;
        memcpy(&pi2, ok_data.data(), sizeof(pi2));
        kp->has_editor = (pi2.flags & 1) != 0; // effFlagsHasEditor = 1
    }
    if (kp->has_editor) {
        std::vector<uint8_t> rect_data;
        if (send_and_wait(kp, IPC_OP_EDITOR_GET_RECT, nullptr, 0, &rect_data) &&
            rect_data.size() >= sizeof(IpcEditorRect)) {
            IpcEditorRect rect;
            memcpy(&rect, rect_data.data(), sizeof(rect));
            kp->editor_width = rect.width;
            kp->editor_height = rect.height;
            fprintf(stderr, "keepsake: editor available (%dx%d)\n",
                    kp->editor_width, kp->editor_height);
        }
    }

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

    size_t shm_size = static_cast<size_t>(kp->num_inputs + kp->num_outputs) *
                      max_frames * sizeof(float);
    if (shm_size == 0) shm_size = max_frames * sizeof(float);

    if (!platform_shm_create(kp->shm, shm_name, shm_size)) return false;

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

    // Copy input audio to shared memory
    auto *shm_base = static_cast<float *>(kp->shm.ptr);
    for (int ch = 0; ch < kp->num_inputs; ch++) {
        float *dst = shm_base + ch * kp->max_frames;
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

    // Forward input events to bridge: MIDI, notes, and param changes
    if (process->in_events) {
        uint32_t ev_count = process->in_events->size(process->in_events);
        for (uint32_t i = 0; i < ev_count; i++) {
            auto *hdr = process->in_events->get(process->in_events, i);

            if (hdr->type == CLAP_EVENT_MIDI) {
                auto *midi = reinterpret_cast<const clap_event_midi_t *>(hdr);
                IpcMidiEventPayload mp;
                mp.delta_frames = static_cast<int32_t>(hdr->time);
                mp.data[0] = midi->data[0];
                mp.data[1] = midi->data[1];
                mp.data[2] = midi->data[2];
                mp.data[3] = 0;
                ipc_write_instance_msg(kp->bridge->proc.pipe_to,
                    IPC_OP_MIDI_EVENT, kp->instance_id, &mp, sizeof(mp));

            } else if (hdr->type == CLAP_EVENT_NOTE_ON) {
                auto *note = reinterpret_cast<const clap_event_note_t *>(hdr);
                IpcMidiEventPayload mp;
                mp.delta_frames = static_cast<int32_t>(hdr->time);
                uint8_t vel = static_cast<uint8_t>(note->velocity * 127.0);
                if (vel == 0) vel = 1; // note-on with vel 0 = note-off
                mp.data[0] = static_cast<uint8_t>(0x90 | (note->channel & 0x0F));
                mp.data[1] = static_cast<uint8_t>(note->key & 0x7F);
                mp.data[2] = vel;
                mp.data[3] = 0;
                ipc_write_instance_msg(kp->bridge->proc.pipe_to,
                    IPC_OP_MIDI_EVENT, kp->instance_id, &mp, sizeof(mp));

            } else if (hdr->type == CLAP_EVENT_NOTE_OFF) {
                auto *note = reinterpret_cast<const clap_event_note_t *>(hdr);
                IpcMidiEventPayload mp;
                mp.delta_frames = static_cast<int32_t>(hdr->time);
                mp.data[0] = static_cast<uint8_t>(0x80 | (note->channel & 0x0F));
                mp.data[1] = static_cast<uint8_t>(note->key & 0x7F);
                mp.data[2] = static_cast<uint8_t>(note->velocity * 127.0);
                mp.data[3] = 0;
                ipc_write_instance_msg(kp->bridge->proc.pipe_to,
                    IPC_OP_MIDI_EVENT, kp->instance_id, &mp, sizeof(mp));

            } else if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
                auto *pv = reinterpret_cast<const clap_event_param_value_t *>(hdr);
                IpcSetParamPayload sp;
                sp.index = static_cast<uint32_t>(pv->param_id);
                sp.value = static_cast<float>(pv->value);
                ipc_write_instance_msg(kp->bridge->proc.pipe_to,
                    IPC_OP_SET_PARAM, kp->instance_id, &sp, sizeof(sp));
            }
        }
    }

    // Send PROCESS command
    IpcProcessPayload pp = { frames };
    if (!ipc_write_instance_msg(kp->bridge->proc.pipe_to, IPC_OP_PROCESS,
                                 kp->instance_id, &pp, sizeof(pp))) {
        kp->crashed = true;
        output_silence(process);
        return CLAP_PROCESS_ERROR;
    }

    // Wait for PROCESS_DONE
    uint32_t resp_op;
    std::vector<uint8_t> resp_payload;
    // Timeout: generous for real-time — 2 seconds
    if (!ipc_read_msg(kp->bridge->proc.pipe_from, resp_op, resp_payload, 2000)) {
        fprintf(stderr, "keepsake: bridge timeout/crash during process\n");
        kp->crashed = true;
        output_silence(process);
        return CLAP_PROCESS_ERROR;
    }

    if (resp_op != IPC_OP_PROCESS_DONE) {
        fprintf(stderr, "keepsake: unexpected response 0x%02X during process\n",
                resp_op);
        kp->crashed = true;
        output_silence(process);
        return CLAP_PROCESS_ERROR;
    }

    // Copy output audio from shared memory
    float *out_base = shm_base + kp->num_inputs * kp->max_frames;
    for (int ch = 0; ch < kp->num_outputs; ch++) {
        float *src = out_base + ch * kp->max_frames;
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

// --- Params extension ---

static uint32_t params_count(const clap_plugin_t *plugin) {
    return static_cast<uint32_t>(get(plugin)->params.size());
}

static bool params_get_info(const clap_plugin_t *plugin, uint32_t index,
                              clap_param_info_t *info) {
    auto *kp = get(plugin);
    if (index >= kp->params.size()) return false;
    const auto &cp = kp->params[index];

    memset(info, 0, sizeof(*info));
    info->id = static_cast<clap_id>(cp.index);
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    snprintf(info->name, sizeof(info->name), "%s", cp.name);
    info->module[0] = '\0';
    info->min_value = 0.0;
    info->max_value = 1.0;
    info->default_value = static_cast<double>(cp.default_value);
    return true;
}

static bool params_get_value(const clap_plugin_t *plugin, clap_id param_id,
                               double *value) {
    auto *kp = get(plugin);
    if (kp->crashed || !kp->bridge_ok) return false;
    // For real-time safety, return cached default. True live value would
    // require bridge round-trip which isn't RT-safe.
    if (param_id < kp->params.size()) {
        *value = static_cast<double>(kp->params[param_id].default_value);
        return true;
    }
    return false;
}

static bool params_value_to_text(const clap_plugin_t *plugin,
                                   clap_id param_id, double value,
                                   char *buf, uint32_t buf_size) {
    auto *kp = get(plugin);
    if (param_id >= kp->params.size()) return false;
    const auto &cp = kp->params[param_id];
    if (cp.label[0]) {
        snprintf(buf, buf_size, "%.2f %s", value, cp.label);
    } else {
        snprintf(buf, buf_size, "%.2f", value);
    }
    return true;
}

static bool params_text_to_value(const clap_plugin_t * /*plugin*/,
                                   clap_id /*param_id*/,
                                   const char *text, double *value) {
    char *end = nullptr;
    double v = strtod(text, &end);
    if (end == text) return false;
    *value = v;
    return true;
}

static void params_flush(const clap_plugin_t *plugin,
                           const clap_input_events_t *in,
                           const clap_output_events_t * /*out*/) {
    auto *kp = get(plugin);
    if (!in || kp->crashed) return;
    uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; i++) {
        auto *hdr = in->get(in, i);
        if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
            auto *pv = reinterpret_cast<const clap_event_param_value_t *>(hdr);
            IpcSetParamPayload sp;
            sp.index = static_cast<uint32_t>(pv->param_id);
            sp.value = static_cast<float>(pv->value);
            ipc_write_instance_msg(kp->bridge->proc.pipe_to,
                IPC_OP_SET_PARAM, kp->instance_id, &sp, sizeof(sp));
        }
    }
}

static const clap_plugin_params_t s_params = {
    .count = params_count,
    .get_info = params_get_info,
    .get_value = params_get_value,
    .value_to_text = params_value_to_text,
    .text_to_value = params_text_to_value,
    .flush = params_flush,
};

// --- State extension ---

static bool state_save(const clap_plugin_t *plugin,
                         const clap_ostream_t *stream) {
    auto *kp = get(plugin);
    if (kp->crashed || !kp->bridge_ok) return false;

    std::vector<uint8_t> chunk_data;
    if (!send_and_wait(kp, IPC_OP_GET_CHUNK, nullptr, 0, &chunk_data))
        return false;

    if (chunk_data.empty()) return true; // plugin has no state

    // Write chunk size then chunk data
    uint32_t size = static_cast<uint32_t>(chunk_data.size());
    if (stream->write(stream, &size, sizeof(size)) != sizeof(size))
        return false;
    if (stream->write(stream, chunk_data.data(), size) !=
        static_cast<int64_t>(size))
        return false;

    return true;
}

static bool state_load(const clap_plugin_t *plugin,
                         const clap_istream_t *stream) {
    auto *kp = get(plugin);
    if (kp->crashed || !kp->bridge_ok) return false;

    uint32_t size = 0;
    if (stream->read(stream, &size, sizeof(size)) != sizeof(size))
        return false;
    if (size == 0) return true;
    if (size > 64 * 1024 * 1024) return false; // sanity limit: 64 MB

    std::vector<uint8_t> chunk(size);
    if (stream->read(stream, chunk.data(), size) != static_cast<int64_t>(size))
        return false;

    return send_and_wait(kp, IPC_OP_SET_CHUNK, chunk.data(), size);
}

static const clap_plugin_state_t s_state = {
    .save = state_save,
    .load = state_load,
};

// --- GUI extension (floating window) ---

static bool gui_is_api_supported(const clap_plugin_t *plugin,
                                   const char *api, bool is_floating) {
    auto *kp = get(plugin);
    if (!kp->has_editor) return false;
#ifdef __APPLE__
    // macOS: floating only (no cross-process NSView embedding)
    if (!is_floating) return false;
    return strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
#elif defined(_WIN32)
    // Windows: both embedded (SetParent) and floating
    return strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
#else
    // Linux: both embedded (XReparentWindow) and floating
    return strcmp(api, CLAP_WINDOW_API_X11) == 0;
#endif
}

static bool gui_get_preferred_api(const clap_plugin_t * /*plugin*/,
                                    const char **api, bool *is_floating) {
#ifdef __APPLE__
    *api = CLAP_WINDOW_API_COCOA;
    *is_floating = true; // macOS: floating only
#elif defined(_WIN32)
    *api = CLAP_WINDOW_API_WIN32;
    *is_floating = false; // Windows: prefer embedded
#else
    *api = CLAP_WINDOW_API_X11;
    *is_floating = false; // Linux: prefer embedded
#endif
    return true;
}

static bool gui_create(const clap_plugin_t *plugin,
                         const char * /*api*/, bool is_floating) {
    auto *kp = get(plugin);
    if (!kp->has_editor || kp->crashed) return false;
#ifdef __APPLE__
    if (!is_floating) return false; // macOS: floating only
#else
    (void)is_floating; // Windows/Linux: both modes supported
#endif
    kp->gui_is_floating = is_floating;
    return true;
}

static void gui_destroy(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    if (kp->editor_open && !kp->crashed) {
        send_and_wait(kp, IPC_OP_EDITOR_CLOSE);
        kp->editor_open = false;
    }
}

static bool gui_set_scale(const clap_plugin_t * /*plugin*/, double /*scale*/) {
    return false; // VST2 doesn't support DPI scaling
}

static bool gui_get_size(const clap_plugin_t *plugin,
                           uint32_t *width, uint32_t *height) {
    auto *kp = get(plugin);
    if (kp->editor_width > 0 && kp->editor_height > 0) {
        *width = static_cast<uint32_t>(kp->editor_width);
        *height = static_cast<uint32_t>(kp->editor_height);
        return true;
    }
    return false;
}

static bool gui_can_resize(const clap_plugin_t * /*plugin*/) {
    return false; // VST2 editors are fixed size
}

static bool gui_get_resize_hints(const clap_plugin_t * /*plugin*/,
                                   clap_gui_resize_hints_t * /*hints*/) {
    return false;
}

static bool gui_adjust_size(const clap_plugin_t * /*plugin*/,
                              uint32_t * /*width*/, uint32_t * /*height*/) {
    return false;
}

static bool gui_set_size(const clap_plugin_t * /*plugin*/,
                           uint32_t /*width*/, uint32_t /*height*/) {
    return false;
}

static bool gui_set_parent(const clap_plugin_t *plugin,
                             const clap_window_t *window) {
    auto *kp = get(plugin);
    if (kp->crashed || !kp->has_editor || !kp->bridge || !window) return false;

    // Send the native window handle to the bridge for embedding
    uint64_t handle = 0;
#ifdef _WIN32
    handle = reinterpret_cast<uint64_t>(window->win32);
#elif defined(__linux__)
    handle = static_cast<uint64_t>(window->x11);
#else
    return false; // macOS doesn't support embedded mode
#endif

    if (!send_and_wait(kp, IPC_OP_EDITOR_SET_PARENT, &handle, sizeof(handle)))
        return false;

    kp->editor_open = true;
    return true;
}

static bool gui_set_transient(const clap_plugin_t * /*plugin*/,
                                const clap_window_t * /*window*/) {
    // Could position the floating window relative to the host window.
    // Not implemented yet.
    return true;
}

static void gui_suggest_title(const clap_plugin_t * /*plugin*/,
                                const char * /*title*/) {
    // Could set the floating window title
}

static bool gui_show(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    if (kp->crashed || !kp->has_editor) return false;
    // For floating mode, send EDITOR_OPEN to create the floating window.
    // For embedded mode, the editor was already opened via set_parent.
    if (kp->gui_is_floating && !kp->editor_open) {
        if (!send_and_wait(kp, IPC_OP_EDITOR_OPEN)) return false;
        kp->editor_open = true;
    }
    return true;
}

static bool gui_hide(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    if (kp->editor_open && !kp->crashed) {
        send_and_wait(kp, IPC_OP_EDITOR_CLOSE);
        kp->editor_open = false;
    }
    return true;
}

static const clap_plugin_gui_t s_gui = {
    .is_api_supported = gui_is_api_supported,
    .get_preferred_api = gui_get_preferred_api,
    .create = gui_create,
    .destroy = gui_destroy,
    .set_scale = gui_set_scale,
    .get_size = gui_get_size,
    .can_resize = gui_can_resize,
    .get_resize_hints = gui_get_resize_hints,
    .adjust_size = gui_adjust_size,
    .set_size = gui_set_size,
    .set_parent = gui_set_parent,
    .set_transient = gui_set_transient,
    .suggest_title = gui_suggest_title,
    .show = gui_show,
    .hide = gui_hide,
};

// --- get_extension ---

static const void *plugin_get_extension(const clap_plugin_t *plugin,
                                          const char *id) {
    if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &s_audio_ports;
    if (strcmp(id, CLAP_EXT_PARAMS) == 0) return &s_params;
    if (strcmp(id, CLAP_EXT_STATE) == 0) return &s_state;
    if (strcmp(id, CLAP_EXT_GUI) == 0 && get(plugin)->has_editor)
        return &s_gui;
    return nullptr;
}

static void plugin_on_main_thread(const clap_plugin_t * /*plugin*/) {
    // No-op
}

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
