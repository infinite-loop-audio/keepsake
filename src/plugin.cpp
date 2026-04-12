//
// Keepsake CLAP plugin — subprocess-bridged VST2 plugin instance.
// Each instance manages its own keepsake-bridge subprocess.
//

#include "plugin_internal.h"
#include <condition_variable>
#include <thread>

// --- Helpers ---

struct AsyncBridgeInitState {
    std::mutex mutex;
    std::condition_variable cv;
    BridgePool *pool = nullptr;
    BridgeProcess *bridge = nullptr;
    const clap_host_t *host = nullptr;
    std::string bridge_binary;
    std::string plugin_path;
    uint32_t format = FORMAT_VST2;
    IsolationMode isolation = IsolationMode::PER_INSTANCE;

    uint32_t instance_id = 0;
    int32_t num_inputs = 0;
    int32_t num_outputs = 0;
    int32_t num_params = 0;
    bool has_editor = false;

    std::string shm_name;
    uint32_t shm_size = 0;
    double sample_rate = 44100.0;
    uint32_t max_frames = 512;
    bool want_activate = false;
    bool want_start_processing = false;
    bool active_sent = false;
    bool start_sent = false;

    bool cancel_requested = false;
    bool completed = false;
    bool success = false;
    bool consumed = false;

    ~AsyncBridgeInitState() {
        if (!bridge || !pool) return;
        if (success) pool->release(bridge);
        else pool->abandon(bridge);
        bridge = nullptr;
    }
};

static bool send_and_wait_bridge(BridgeProcess *bridge,
                                  uint32_t instance_id,
                                  uint32_t opcode,
                                  const void *payload = nullptr,
                                  uint32_t size = 0,
                                  std::vector<uint8_t> *ok_payload = nullptr,
                                  int timeout_ms = 3000) {
    if (!bridge) return false;
    if (!ipc_write_instance_msg(bridge->proc.pipe_to, opcode,
                                instance_id, payload, size))
        return false;

    uint32_t resp_op;
    std::vector<uint8_t> resp_payload;
    if (!ipc_read_msg(bridge->proc.pipe_from, resp_op, resp_payload, timeout_ms))
        return false;
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
        kp->crashed = true;
    }
    return ok;
}

// Bridge pool pointer — set during factory init
static BridgePool *s_pool = nullptr;
static constexpr int32_t kMaxBridgeChannels = 64;
static constexpr size_t kMaxBridgeShmBytes = 64u * 1024u * 1024u;

void keepsake_plugin_set_pool(BridgePool *pool) { s_pool = pool; }

void sync_async_init(KeepsakePlugin *kp) {
    auto state = kp->async_init;
    if (!state || kp->bridge_ok || kp->crashed) return;

    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->completed || state->consumed) return;
    state->consumed = true;

    if (!state->success) {
        kp->crashed = true;
        return;
    }

    kp->bridge = state->bridge;
    state->bridge = nullptr;
    kp->instance_id = state->instance_id;
    kp->num_inputs = state->num_inputs;
    kp->num_outputs = state->num_outputs;
    kp->num_params = state->num_params;
    kp->has_editor = state->has_editor;
    kp->bridge_ok = true;
}

bool wait_async_init(KeepsakePlugin *kp, int timeout_ms) {
    auto state = kp->async_init;
    if (!state || kp->bridge_ok || kp->crashed) return kp->bridge_ok;

    std::unique_lock<std::mutex> lock(state->mutex);
    if (!state->completed) {
        state->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                           [&]() { return state->completed; });
    }
    lock.unlock();
    sync_async_init(kp);
    return kp->bridge_ok && !kp->crashed;
}

static void request_host_callback(const clap_host_t *host) {
    if (host && host->request_callback) host->request_callback(host);
}

static void request_host_refresh(const clap_host_t *host) {
    if (!host) return;
    if (host->request_restart) host->request_restart(host);
    if (host->request_callback) host->request_callback(host);
}

static void queue_async_activation(KeepsakePlugin *kp,
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

static void clear_async_queue(KeepsakePlugin *kp, bool clear_activate) {
    auto state = kp->async_init;
    if (!state) return;
    std::lock_guard<std::mutex> lock(state->mutex);
    if (clear_activate) state->want_activate = false;
    state->want_start_processing = false;
}

static void launch_async_init(KeepsakePlugin *kp) {
    auto state = std::make_shared<AsyncBridgeInitState>();
    state->pool = s_pool;
    state->host = kp->host;
    state->bridge_binary = kp->bridge_binary;
    state->plugin_path = kp->vst2_path;
    state->format = kp->format;
    state->isolation = kp->isolation;
    kp->async_init = state;

    std::thread([state]() {
        BridgeProcess *bridge = state->pool->acquire(
            state->bridge_binary, state->plugin_path, state->format,
            state->isolation);
        if (!bridge) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->completed = true;
            state->cv.notify_all();
            request_host_refresh(state->host);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->cancel_requested) {
                state->completed = true;
                state->cv.notify_all();
                request_host_refresh(state->host);
                return;
            }
            state->bridge = bridge;
        }

        std::vector<uint8_t> init_payload(4 + state->plugin_path.size());
        memcpy(init_payload.data(), &state->format, 4);
        memcpy(init_payload.data() + 4, state->plugin_path.data(),
               state->plugin_path.size());

        uint32_t instance_id = 0;
        int32_t num_inputs = 0;
        int32_t num_outputs = 0;
        int32_t num_params = 0;
        bool has_editor = false;
        std::vector<uint8_t> ok_data;
        bool ok = send_and_wait_bridge(bridge, 0, IPC_OP_INIT,
                                       init_payload.data(),
                                       static_cast<uint32_t>(init_payload.size()),
                                       &ok_data, 10000);
        if (ok && ok_data.size() >= 4 + sizeof(IpcPluginInfo)) {
            memcpy(&instance_id, ok_data.data(), 4);
            IpcPluginInfo pi;
            memcpy(&pi, ok_data.data() + 4, sizeof(pi));
            num_inputs = pi.num_inputs;
            num_outputs = pi.num_outputs;
            num_params = pi.num_params;
            has_editor = (pi.flags & 1) != 0;
        } else {
            ok = false;
        }

        if (ok) {
            std::string shm_name;
            uint32_t shm_size = 0;
            double sample_rate = 44100.0;
            uint32_t max_frames = 512;
            bool want_activate = false;
            bool want_start_processing = false;

            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->cancel_requested) ok = false;
                shm_name = state->shm_name;
                shm_size = state->shm_size;
                sample_rate = state->sample_rate;
                max_frames = state->max_frames;
                want_activate = state->want_activate;
                want_start_processing = state->want_start_processing;
            }

            if (ok && want_activate) {
                uint32_t name_len = static_cast<uint32_t>(shm_name.size());
                std::vector<uint8_t> shm_payload(4 + name_len + 4);
                memcpy(shm_payload.data(), &name_len, 4);
                memcpy(shm_payload.data() + 4, shm_name.data(), name_len);
                memcpy(shm_payload.data() + 4 + name_len, &shm_size, 4);

                ok = send_and_wait_bridge(bridge, instance_id, IPC_OP_SET_SHM,
                                          shm_payload.data(),
                                          static_cast<uint32_t>(shm_payload.size()),
                                          nullptr, 3000);
                if (ok) {
                    IpcActivatePayload ap = { sample_rate, max_frames };
                    ok = send_and_wait_bridge(bridge, instance_id, IPC_OP_ACTIVATE,
                                              &ap, sizeof(ap), nullptr, 3000);
                }
                if (ok) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->active_sent = true;
                }
            }

            if (ok && want_start_processing) {
                ok = send_and_wait_bridge(bridge, instance_id, IPC_OP_START_PROC,
                                          nullptr, 0, nullptr, 3000);
                if (ok) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->start_sent = true;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!ok || state->cancel_requested) {
                state->success = false;
            } else {
                state->instance_id = instance_id;
                state->num_inputs = num_inputs;
                state->num_outputs = num_outputs;
                state->num_params = num_params;
                state->has_editor = has_editor;
                state->success = true;
            }
            state->completed = true;
            state->cv.notify_all();
        }

        if (!ok) {
            bool owns_bridge = false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                owns_bridge = (state->bridge == bridge);
                if (owns_bridge) state->bridge = nullptr;
            }
            if (owns_bridge) state->pool->abandon(bridge);
        }

        request_host_refresh(state->host);
    }).detach();
}

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
    launch_async_init(kp);
    return true;
}

static void plugin_destroy(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    if (kp->async_init) {
        auto state = kp->async_init;
        BridgeProcess *pending_bridge = nullptr;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->cancel_requested = true;
            pending_bridge = state->bridge;
        }
        if (pending_bridge) state->pool->terminate(pending_bridge);
        std::unique_lock<std::mutex> lock(state->mutex);
        state->cv.wait_for(lock, std::chrono::milliseconds(250),
                           [&]() { return state->completed; });
    }
    if (kp->bridge_ok && !kp->crashed && kp->bridge) {
        send_and_wait(kp, IPC_OP_SHUTDOWN);
    }
    if (kp->bridge && s_pool) {
        if (kp->crashed) s_pool->abandon(kp->bridge);
        else s_pool->release(kp->bridge);
        kp->bridge = nullptr;
    }
    kp->async_init.reset();
    if (kp->shm.ptr) platform_shm_close(kp->shm);
    delete kp;
}

static bool plugin_activate(const clap_plugin_t *plugin,
                              double sample_rate,
                              uint32_t min_frames,
                              uint32_t max_frames) {
    auto *kp = get(plugin);
    sync_async_init(kp);
    if (kp->crashed) return false;
    (void)min_frames;

    kp->max_frames = max_frames;

    if (kp->num_inputs < 0 || kp->num_outputs < 0 ||
        kp->num_inputs > kMaxBridgeChannels ||
        kp->num_outputs > kMaxBridgeChannels) {
        fprintf(stderr,
                "keepsake: refusing activation with invalid I/O counts in=%d out=%d\n",
                kp->num_inputs, kp->num_outputs);
        kp->crashed = true;
        return false;
    }

    char instance_id[32];
    snprintf(instance_id, sizeof(instance_id), "%p", static_cast<void *>(kp));
    std::string shm_name = platform_shm_name(instance_id);
    size_t shm_size = shm_total_size(kp->num_inputs, kp->num_outputs, max_frames);

    if (shm_size == 0 || shm_size > kMaxBridgeShmBytes) {
        fprintf(stderr,
                "keepsake: refusing activation with invalid shared memory size=%zu in=%d out=%d frames=%u\n",
                shm_size, kp->num_inputs, kp->num_outputs, max_frames);
        kp->crashed = true;
        return false;
    }

    if (!platform_shm_create(kp->shm, shm_name, shm_size)) return false;
    shm_init_sync(shm_control(kp->shm.ptr));

    uint32_t name_len = static_cast<uint32_t>(shm_name.size());
    uint32_t shm_size32 = static_cast<uint32_t>(shm_size);
    std::vector<uint8_t> shm_payload(4 + name_len + 4);
    memcpy(shm_payload.data(), &name_len, 4);
    memcpy(shm_payload.data() + 4, shm_name.data(), name_len);
    memcpy(shm_payload.data() + 4 + name_len, &shm_size32, 4);

    if (kp->bridge_ok) {
        if (!send_and_wait(kp, IPC_OP_SET_SHM, shm_payload.data(),
                           static_cast<uint32_t>(shm_payload.size()))) {
            platform_shm_close(kp->shm);
            return false;
        }

        IpcActivatePayload ap = { sample_rate, max_frames };
        if (!send_and_wait(kp, IPC_OP_ACTIVATE, &ap, sizeof(ap))) {
            platform_shm_close(kp->shm);
            return false;
        }
    } else {
        queue_async_activation(kp, shm_name, shm_size32, sample_rate,
                               max_frames, false);
    }

    kp->active = true;
    return true;
}

static void plugin_deactivate(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    sync_async_init(kp);
    clear_async_queue(kp, true);
    if (kp->bridge_ok && kp->active && !kp->crashed) {
        send_and_wait(kp, IPC_OP_DEACTIVATE);
    }
    if (kp->shm.ptr) platform_shm_close(kp->shm);
    kp->active = false;
    kp->max_frames = 0;
}

static bool plugin_start_processing(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    sync_async_init(kp);
    if (!kp->active || kp->crashed) return false;
    if (kp->bridge_ok) {
        if (!send_and_wait(kp, IPC_OP_START_PROC)) return false;
    } else {
        auto state = kp->async_init;
        if (state) {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->want_start_processing = true;
        }
    }
    kp->processing = true;
    return true;
}

static void plugin_stop_processing(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    sync_async_init(kp);
    clear_async_queue(kp, false);
    if (kp->bridge_ok && kp->processing && !kp->crashed) {
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
    if (!kp->bridge_ok) sync_async_init(kp);

    if (kp->crashed || !kp->bridge_ok || !kp->processing || !kp->shm.ptr) {
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
    sync_async_init(kp);
    if (is_input) return kp->num_inputs > 0 ? 1 : 0;
    return kp->num_outputs > 0 ? 1 : 0;
}

static bool audio_ports_get(const clap_plugin_t *plugin, uint32_t index,
                              bool is_input, clap_audio_port_info_t *info) {
    auto *kp = get(plugin);
    sync_async_init(kp);
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
    sync_async_init(get(plugin));
    if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &s_audio_ports;
    if (strcmp(id, CLAP_EXT_PARAMS) == 0) return &keepsake_params_ext;
    if (strcmp(id, CLAP_EXT_STATE) == 0) return &keepsake_state_ext;
    if (strcmp(id, CLAP_EXT_LATENCY) == 0) return &s_latency;
    if (strcmp(id, CLAP_EXT_GUI) == 0 && get(plugin)->has_editor)
        return &keepsake_gui_ext;
    return nullptr;
}

static void plugin_on_main_thread(const clap_plugin_t *plugin) {
    sync_async_init(get(plugin));
}

// --- Factory entry point ---

const clap_plugin_t *keepsake_plugin_create(
    const clap_host_t *host,
    const clap_plugin_descriptor_t *descriptor,
    const std::string &plugin_path,
    const std::string &bridge_binary,
    int32_t num_inputs,
    int32_t num_outputs,
    int32_t num_params,
    bool has_editor,
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
    kp->num_params = num_params;
    kp->has_editor = has_editor;
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
