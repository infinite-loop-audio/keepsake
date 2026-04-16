//
// Keepsake CLAP plugin processing and extension callbacks.
//

#include "plugin_internal.h"
#ifdef __APPLE__
#include "plugin_gui_mac_embed.h"
#endif
#include "bridge_gui.h"
#include "debug_log.h"

#include <clap/events.h>
#ifndef _WIN32
#include <ctime>
#endif

#ifdef _WIN32
#include <windows.h>
static const uint64_t GUI_SIZE_POLL_INTERVAL_MS = 100;
static const uint64_t GUI_HOST_RESIZE_MIN_INTERVAL_MS = 33;
static const uint64_t GUI_DIRECT_RESIZE_FALLBACK_SUPPRESS_MS = 500;
#endif

static void maybe_request_gui_main_thread(KeepsakePlugin *kp);

#ifndef _WIN32
static uint64_t monotonic_now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000u +
           static_cast<uint64_t>(ts.tv_nsec) / 1000000u;
}
#endif

#ifdef _WIN32
static bool maybe_apply_editor_resize_request(KeepsakePlugin *kp, ShmProcessControl *ctrl) {
    if (!ctrl || !keepsake_gui_session_can_host_resize(kp)) {
        return false;
    }

    const uint32_t resize_serial = shm_load_acquire(&ctrl->editor_resize_serial);
    if (resize_serial == 0 || resize_serial == kp->last_seen_editor_resize_serial) {
        return false;
    }

    const int32_t raw_width = ctrl->editor_resize_width;
    const int32_t raw_height = ctrl->editor_resize_height;
    if (raw_width <= 0 || raw_height <= 0) {
        kp->last_seen_editor_resize_serial = resize_serial;
        return false;
    }

    const int32_t host_width = raw_width;
    const int32_t host_height = raw_height + KEEPSAKE_EDITOR_HEADER_HEIGHT;
    const uint64_t now_ms = GetTickCount64();
    if (keepsake_gui_session_should_rate_limit_resize(kp,
                                                      host_width,
                                                      host_height,
                                                      now_ms,
                                                      GUI_HOST_RESIZE_MIN_INTERVAL_MS)) {
        return false;
    }

    keepsake_debug_log("keepsake: request_resize direct raw=%dx%d host=%dx%d serial=%u\n",
                       raw_width, raw_height, host_width, host_height, resize_serial);
    if (!kp->host_gui->request_resize(kp->host,
                                      static_cast<uint32_t>(host_width),
                                      static_cast<uint32_t>(host_height))) {
        return false;
    }

    keepsake_gui_session_record_host_resize_request(kp,
                                                    raw_width,
                                                    raw_height,
                                                    host_width,
                                                    host_height,
                                                    resize_serial,
                                                    now_ms,
                                                    true);
    return true;
}
#endif

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

clap_process_status plugin_process(const clap_plugin_t *plugin,
                                   const clap_process_t *process) {
    auto *kp = get(plugin);
    if (!kp->bridge_ok) sync_async_init(kp);

    if (kp->crashed || !kp->bridge_ok || !kp->processing || !kp->shm.ptr) {
        output_silence(process);
        return kp->crashed ? CLAP_PROCESS_ERROR : CLAP_PROCESS_SLEEP;
    }

    uint32_t frames = process->frames_count;
    if (frames > kp->max_frames) frames = kp->max_frames;

    auto *ctrl = shm_control(kp->shm.ptr);
    const uint32_t pre_state = shm_load_acquire(&ctrl->state);
    if (pre_state == SHM_STATE_PROCESS_REQUESTED || pre_state == SHM_STATE_PROCESSING) {
        keepsake_debug_log("keepsake: process skipped while prior request still active state=%u\n",
                           pre_state);
        output_silence(process);
        return CLAP_PROCESS_CONTINUE;
    }
    if (pre_state == SHM_STATE_PROCESS_DONE) {
        // Drop late completions from an earlier timed-out block before posting fresh work.
        shm_store_release(&ctrl->state, SHM_STATE_IDLE);
    }

    for (int ch = 0; ch < kp->num_inputs; ch++) {
        float *dst = shm_audio_inputs(kp->shm.ptr, ch, kp->max_frames);
        if (process->audio_inputs_count > 0 &&
            ch < static_cast<int>(process->audio_inputs[0].channel_count) &&
            process->audio_inputs[0].data32 &&
            process->audio_inputs[0].data32[ch]) {
            memcpy(dst, process->audio_inputs[0].data32[ch], frames * sizeof(float));
        } else {
            memset(dst, 0, frames * sizeof(float));
        }
    }

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
                me->data[0] = midi->data[0];
                me->data[1] = midi->data[1];
                me->data[2] = midi->data[2];
                me->data[3] = 0;
            } else if (hdr->type == CLAP_EVENT_NOTE_ON &&
                       midi_idx < SHM_MAX_MIDI_EVENTS) {
                auto *note = reinterpret_cast<const clap_event_note_t *>(hdr);
                me = &ctrl->midi_events[midi_idx++];
                me->delta_frames = static_cast<int32_t>(hdr->time);
                uint8_t vel = static_cast<uint8_t>(note->velocity * 127.0);
                if (vel == 0) vel = 1;
                me->data[0] = static_cast<uint8_t>(0x90 | (note->channel & 0x0F));
                me->data[1] = static_cast<uint8_t>(note->key & 0x7F);
                me->data[2] = vel;
                me->data[3] = 0;
            } else if (hdr->type == CLAP_EVENT_NOTE_OFF &&
                       midi_idx < SHM_MAX_MIDI_EVENTS) {
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

    ctrl->num_frames = frames;
    ctrl->midi_count = midi_idx;
    ctrl->param_count = param_idx;
    memset(&ctrl->transport, 0, sizeof(ctrl->transport));
    int64_t steady_time = process->steady_time;
    if (steady_time < 0) {
        steady_time = kp->process_steady_time;
    }
    ctrl->transport.steady_time = steady_time;
    kp->process_steady_time = steady_time + frames;
    if (process->transport) {
        const auto *transport = process->transport;
        ctrl->transport.flags = transport->flags;
        ctrl->transport.song_pos_beats =
            static_cast<double>(transport->song_pos_beats) /
            static_cast<double>(CLAP_BEATTIME_FACTOR);
        ctrl->transport.tempo = transport->tempo;
        ctrl->transport.tempo_inc = transport->tempo_inc;
        ctrl->transport.loop_start_beats =
            static_cast<double>(transport->loop_start_beats) /
            static_cast<double>(CLAP_BEATTIME_FACTOR);
        ctrl->transport.loop_end_beats =
            static_cast<double>(transport->loop_end_beats) /
            static_cast<double>(CLAP_BEATTIME_FACTOR);
        ctrl->transport.bar_start_beats =
            static_cast<double>(transport->bar_start) /
            static_cast<double>(CLAP_BEATTIME_FACTOR);
        ctrl->transport.bar_number = transport->bar_number;
        ctrl->transport.tsig_num = transport->tsig_num;
        ctrl->transport.tsig_denom = transport->tsig_denom;
    }

#ifndef _WIN32
    pthread_mutex_lock(&ctrl->mutex);
    ctrl->state = SHM_STATE_PROCESS_REQUESTED;
    pthread_cond_signal(&ctrl->cond);

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 100000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
    }

    while (ctrl->state != SHM_STATE_PROCESS_DONE) {
        int r = pthread_cond_timedwait(&ctrl->cond, &ctrl->mutex, &deadline);
        if (r != 0) break;
    }

    bool done = (ctrl->state == SHM_STATE_PROCESS_DONE);
    if (done) ctrl->state = SHM_STATE_IDLE;
    pthread_mutex_unlock(&ctrl->mutex);

    if (!done) {
        output_silence(process);
        return CLAP_PROCESS_CONTINUE;
    }
#else
    keepsake_debug_log("keepsake: process request frames=%u midi=%u params=%u state=%u\n",
                       frames, midi_idx, param_idx, shm_load_acquire(&ctrl->state));
    shm_store_release(&ctrl->state, SHM_STATE_PROCESS_REQUESTED);
#ifdef _WIN32
    if (kp->shm_request_event != INVALID_HANDLE_VALUE) {
        SetEvent(kp->shm_request_event);
    }
#endif

    uint64_t deadline = GetTickCount64() + 5;
    bool done = false;
    while (GetTickCount64() < deadline) {
#ifdef _WIN32
        if (kp->shm_done_event != INVALID_HANDLE_VALUE) {
            DWORD wait_ms = static_cast<DWORD>(deadline - GetTickCount64());
            if (WaitForSingleObject(kp->shm_done_event, wait_ms) == WAIT_OBJECT_0) {
                if (shm_load_acquire(&ctrl->state) == SHM_STATE_PROCESS_DONE) {
                    done = true;
                    break;
                }
            }
        }
#endif
        uint32_t state = shm_load_acquire(&ctrl->state);
        if (state == SHM_STATE_PROCESS_DONE) {
            done = true;
            break;
        }
        SwitchToThread();
    }

    keepsake_debug_log("keepsake: process wait done=%d final_state=%u\n",
                       done ? 1 : 0, shm_load_acquire(&ctrl->state));
    if (done) {
        shm_store_release(&ctrl->state, SHM_STATE_IDLE);
    }

    if (!done) {
        keepsake_debug_log("keepsake: process timeout -> silence\n");
        output_silence(process);
        return CLAP_PROCESS_CONTINUE;
    }
#endif

    for (int ch = 0; ch < kp->num_outputs; ch++) {
        float *src = shm_audio_outputs(kp->shm.ptr, kp->num_inputs, ch, kp->max_frames);
        if (process->audio_outputs_count > 0 &&
            ch < static_cast<int>(process->audio_outputs[0].channel_count) &&
            process->audio_outputs[0].data32 &&
            process->audio_outputs[0].data32[ch]) {
            memcpy(process->audio_outputs[0].data32[ch], src, frames * sizeof(float));
        }
    }

    maybe_request_gui_main_thread(kp);
    return CLAP_PROCESS_CONTINUE;
}

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
    info->port_type = info->channel_count == 2 ? CLAP_PORT_STEREO : CLAP_PORT_MONO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

static const clap_plugin_audio_ports_t s_audio_ports = {
    .count = audio_ports_count,
    .get = audio_ports_get,
};

static uint32_t note_ports_count(const clap_plugin_t *plugin, bool is_input) {
    auto *kp = get(plugin);
    sync_async_init(kp);
    if (is_input) return 1;
    return 0;
}

static bool note_ports_get(const clap_plugin_t *plugin, uint32_t index,
                           bool is_input, clap_note_port_info_t *info) {
    auto *kp = get(plugin);
    sync_async_init(kp);
    if (!is_input || index != 0) return false;

    memset(info, 0, sizeof(*info));
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    snprintf(info->name, sizeof(info->name), "%s", "Note Input");
    return true;
}

static const clap_plugin_note_ports_t s_note_ports = {
    .count = note_ports_count,
    .get = note_ports_get,
};

extern const clap_plugin_params_t keepsake_params_ext;
extern const clap_plugin_state_t keepsake_state_ext;
extern const clap_plugin_gui_t keepsake_gui_ext;

static uint32_t plugin_latency_get(const clap_plugin_t *plugin) {
    return get(plugin)->max_frames;
}

static const clap_plugin_latency_t s_latency = {
    .get = plugin_latency_get,
};

const void *plugin_get_extension(const clap_plugin_t *plugin, const char *id) {
    sync_async_init(get(plugin));
    if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &s_audio_ports;
    if (strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &s_note_ports;
    if (strcmp(id, CLAP_EXT_PARAMS) == 0) return &keepsake_params_ext;
    if (strcmp(id, CLAP_EXT_STATE) == 0) return &keepsake_state_ext;
    if (strcmp(id, CLAP_EXT_LATENCY) == 0) return &s_latency;
    if (strcmp(id, CLAP_EXT_GUI) == 0 && get(plugin)->has_editor) {
        return &keepsake_gui_ext;
    }
    return nullptr;
}

void plugin_on_main_thread(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    sync_async_init(kp);
    if (kp->crashed || !kp->bridge_ok || !kp->shm.ptr) return;
    keepsake_gui_session_clear_callback_request(kp);

    auto *ctrl = shm_control(kp->shm.ptr);
#ifdef __APPLE__
    if (kp->gui_iosurface_embed) {
        if (kp->gui_embed_refresh_burst_remaining > 0) {
            gui_mac_refresh_iosurface(kp);
        }
    }
#endif
    if (kp->gui_iosurface_embed && kp->gui_embed_refresh_burst_remaining > 0) {
        kp->gui_embed_refresh_burst_remaining -= 1;
        keepsake_gui_session_request_callback_once(kp);
    }
#ifdef _WIN32
    if (keepsake_gui_session_can_host_resize(kp)) {
        if (!maybe_apply_editor_resize_request(kp, ctrl)) {
            const uint64_t now_ms = GetTickCount64();
            if (keepsake_gui_session_should_suppress_poll(kp,
                                                          now_ms,
                                                          GUI_DIRECT_RESIZE_FALLBACK_SUPPRESS_MS)) {
                return;
            }
            std::vector<uint8_t> payload;
            if (send_and_wait(kp, IPC_OP_EDITOR_GET_RECT, nullptr, 0, &payload, 10) &&
                payload.size() >= sizeof(IpcEditorRect)) {
                IpcEditorRect rect{};
                memcpy(&rect, payload.data(), sizeof(rect));
                if (rect.width > 0 && rect.height > 0 &&
                    (rect.width != kp->editor_width || rect.height != kp->editor_height)) {
                    const int32_t host_width = rect.width;
                    const int32_t host_height = rect.height + KEEPSAKE_EDITOR_HEADER_HEIGHT;
                    if (keepsake_gui_session_should_rate_limit_resize(kp,
                                                                      host_width,
                                                                      host_height,
                                                                      now_ms,
                                                                      GUI_HOST_RESIZE_MIN_INTERVAL_MS)) {
                        return;
                    }
                    keepsake_debug_log("keepsake: request_resize polled raw=%dx%d host=%dx%d\n",
                                       rect.width, rect.height, host_width, host_height);
                    if (kp->host_gui->request_resize(kp->host,
                                                     static_cast<uint32_t>(host_width),
                                                     static_cast<uint32_t>(host_height))) {
                        keepsake_gui_session_record_host_resize_request(kp,
                                                                        rect.width,
                                                                        rect.height,
                                                                        host_width,
                                                                        host_height,
                                                                        0,
                                                                        now_ms,
                                                                        false);
                    }
                }
            }
        }
    }
#endif

    if (!kp->editor_open_pending) return;

    const KeepsakeGuiPendingState pending_state =
        keepsake_gui_session_get_pending_state(kp);
    if (pending_state == KeepsakeGuiPendingState::Open) {
        keepsake_debug_log("keepsake: editor pending complete instance=%u\n",
                           kp->instance_id);
        gui_complete_pending_open(kp);
    } else if (pending_state == KeepsakeGuiPendingState::Opening) {
        keepsake_gui_session_request_callback_once(kp);
    } else {
        const uint32_t editor_state = shm_load_acquire(&ctrl->editor_state);
        keepsake_debug_log("keepsake: editor pending terminal state=%u instance=%u\n",
                           editor_state, kp->instance_id);
        if (kp->bridge && platform_process_alive(kp->bridge->proc)) {
            abandon_bridge(kp, "pending editor open failed");
        } else {
            kp->crashed = true;
        }
    }
}

static void maybe_request_gui_main_thread(KeepsakePlugin *kp) {
    if (!kp) return;

    if (kp->editor_open_pending) {
        keepsake_gui_session_request_callback_once(kp);
        return;
    }

#ifdef __APPLE__
    if (kp->editor_open && kp->gui_iosurface_embed) {
        if (kp->gui_embed_refresh_burst_remaining > 0) {
            keepsake_gui_session_request_callback_once(kp);
        }
    }
#endif

#ifdef _WIN32
    if (kp->editor_open && !kp->gui_is_floating) {
        if (kp->shm.ptr) {
            auto *ctrl = shm_control(kp->shm.ptr);
            if (shm_load_acquire(&ctrl->editor_resize_serial) != kp->last_seen_editor_resize_serial) {
                keepsake_gui_session_request_callback_once(kp);
                return;
            }
        }
        uint64_t now_ms = GetTickCount64();
        if (now_ms - kp->last_gui_poll_ms >= GUI_SIZE_POLL_INTERVAL_MS) {
            kp->last_gui_poll_ms = now_ms;
            keepsake_gui_session_request_callback_once(kp);
        }
    }
#endif
}
