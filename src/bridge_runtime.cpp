//
// keepsake-bridge runtime helpers — instance state and non-GUI IPC handlers.
//

#include "bridge_runtime.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef _WIN32
#include <unistd.h>
#endif

std::unordered_map<uint32_t, PluginInstance *> g_instances;
static uint32_t g_next_instance_id = 1;

PluginInstance *get_instance(uint32_t id) {
    auto it = g_instances.find(id);
    return (it != g_instances.end()) ? it->second : nullptr;
}

void destroy_instance(uint32_t id) {
    auto it = g_instances.find(id);
    if (it == g_instances.end()) return;
    auto *inst = it->second;
    if (inst->loader) {
        if (inst->active) inst->loader->deactivate();
        inst->loader->close();
        delete inst->loader;
    }
    if (inst->shm.ptr) platform_shm_close(inst->shm);
    delete inst;
    g_instances.erase(it);
}

void destroy_all_instances() {
    std::vector<uint32_t> ids;
    for (auto &kv : g_instances) ids.push_back(kv.first);
    for (auto id : ids) destroy_instance(id);
}

#ifndef _WIN32
static void *audio_thread_func(void *arg) {
    auto *inst = static_cast<PluginInstance *>(arg);
    auto *ctrl = shm_control(inst->shm.ptr);

    std::vector<float *> in_ptrs(static_cast<size_t>(inst->num_inputs));
    std::vector<float *> out_ptrs(static_cast<size_t>(inst->num_outputs));

    fprintf(stderr, "bridge: audio thread started for instance %u\n", inst->id);

    while (inst->active) {
        pthread_mutex_lock(&ctrl->mutex);

        while (ctrl->state != SHM_STATE_PROCESS_REQUESTED && inst->active) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 50000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&ctrl->cond, &ctrl->mutex, &ts);
        }

        if (!inst->active) {
            pthread_mutex_unlock(&ctrl->mutex);
            break;
        }

        uint32_t nframes = ctrl->num_frames;
        if (nframes > inst->max_frames) nframes = inst->max_frames;

        for (uint32_t p = 0; p < ctrl->param_count && p < 64; p++) {
            inst->loader->set_param(ctrl->params[p].index, ctrl->params[p].value);
        }
        for (uint32_t m = 0; m < ctrl->midi_count && m < SHM_MAX_MIDI_EVENTS; m++) {
            inst->loader->send_midi(ctrl->midi_events[m].delta_frames,
                                    ctrl->midi_events[m].data);
        }

        for (int i = 0; i < inst->num_inputs; i++) {
            in_ptrs[static_cast<size_t>(i)] =
                shm_audio_inputs(inst->shm.ptr, i, inst->max_frames);
        }
        for (int i = 0; i < inst->num_outputs; i++) {
            out_ptrs[static_cast<size_t>(i)] =
                shm_audio_outputs(inst->shm.ptr, inst->num_inputs, i, inst->max_frames);
        }

        inst->loader->process(
            inst->num_inputs > 0 ? in_ptrs.data() : nullptr,
            inst->num_inputs,
            inst->num_outputs > 0 ? out_ptrs.data() : nullptr,
            inst->num_outputs,
            nframes);

        ctrl->state = SHM_STATE_PROCESS_DONE;
        pthread_cond_signal(&ctrl->cond);
        pthread_mutex_unlock(&ctrl->mutex);
    }

    fprintf(stderr, "bridge: audio thread stopped for instance %u\n", inst->id);
    return nullptr;
}
#endif

void handle_init(uint32_t /*caller_id*/, const std::vector<uint8_t> &payload) {
    if (payload.size() < 5) {
        ipc_write_error(g_pipe_out, "INIT payload too small");
        return;
    }

    uint32_t format_id;
    memcpy(&format_id, payload.data(), sizeof(format_id));
    std::string path(payload.begin() + 4, payload.end());

    auto *loader = create_loader(static_cast<PluginFormat>(format_id));
    if (!loader) {
        ipc_write_error(g_pipe_out, "unsupported format");
        return;
    }

    bool load_on_main_thread = false;
#if defined(__APPLE__)
    load_on_main_thread = (format_id == FORMAT_VST2);
#endif

    if (load_on_main_thread) {
        if (!loader->load(path)) {
            ipc_write_error(g_pipe_out, "failed to load plugin");
            delete loader;
            return;
        }
    } else {
#ifndef _WIN32
        volatile bool load_done = false;
        volatile bool load_ok = false;

        pthread_t lt;
        struct LoadCtx {
            BridgeLoader *loader;
            std::string path;
            volatile bool *done;
            volatile bool *ok;
        };
        auto *ctx = new LoadCtx{loader, path, &load_done, &load_ok};

        pthread_create(&lt, nullptr, [](void *arg) -> void * {
            auto *c = static_cast<LoadCtx *>(arg);
            *c->ok = c->loader->load(c->path);
            *c->done = true;
            delete c;
            return nullptr;
        }, ctx);

        for (int i = 0; i < 50 && !load_done; i++) usleep(100000);

        if (!load_done) {
            fprintf(stderr, "bridge: plugin load timed out (5s): %s\n", path.c_str());
            pthread_detach(lt);
            ipc_write_error(g_pipe_out, "plugin load timed out");
            return;
        }
        pthread_join(lt, nullptr);

        if (!load_ok) {
            ipc_write_error(g_pipe_out, "failed to load plugin");
            delete loader;
            return;
        }
#else
        if (!loader->load(path)) {
            ipc_write_error(g_pipe_out, "failed to load plugin");
            delete loader;
            return;
        }
#endif
    }

    auto *inst = new PluginInstance();
    inst->id = g_next_instance_id++;
    inst->loader = loader;

    IpcPluginInfo info = {};
    std::vector<uint8_t> extra;
    loader->get_info(info, extra);
    inst->num_inputs = info.num_inputs;
    inst->num_outputs = info.num_outputs;

    g_instances[inst->id] = inst;

    size_t total = 4 + sizeof(info) + extra.size();
    std::vector<uint8_t> resp(total);
    memcpy(resp.data(), &inst->id, 4);
    memcpy(resp.data() + 4, &info, sizeof(info));
    if (!extra.empty()) {
        memcpy(resp.data() + 4 + sizeof(info), extra.data(), extra.size());
    }

    ipc_write_ok(g_pipe_out, resp.data(), static_cast<uint32_t>(total));
    fprintf(stderr, "bridge: created instance %u for '%s'\n", inst->id, path.c_str());
}

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

    if (inst->shm.ptr) platform_shm_close(inst->shm);
    if (!platform_shm_open(inst->shm, name, shm_size)) {
        ipc_write_error(g_pipe_out, "failed to open shared memory");
        return;
    }
    ipc_write_ok(g_pipe_out);
}

void handle_activate(PluginInstance *inst, const std::vector<uint8_t> &payload) {
    if (!inst->loader || payload.size() < sizeof(IpcActivatePayload)) {
        ipc_write_error(g_pipe_out, "ACTIVATE: not ready");
        return;
    }
    IpcActivatePayload ap;
    memcpy(&ap, payload.data(), sizeof(ap));
    inst->max_frames = ap.max_frames;
    inst->loader->activate(ap.sample_rate, ap.max_frames);
    inst->active = true;

#ifndef _WIN32
    if (inst->shm.ptr) {
        pthread_create(&inst->audio_thread, nullptr, audio_thread_func, inst);
    }
#endif

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
    if (inst->loader && inst->loader->get_editor_rect(w, h)) {
        rect.width = w;
        rect.height = h;
    }
    ipc_write_ok(g_pipe_out, &rect, sizeof(rect));
}

void handle_deactivate(PluginInstance *inst) {
    if (inst->loader && inst->active) {
        inst->active = false;
#ifndef _WIN32
        if (inst->shm.ptr) {
            auto *dc = shm_control(inst->shm.ptr);
            pthread_mutex_lock(&dc->mutex);
            pthread_cond_signal(&dc->cond);
            pthread_mutex_unlock(&dc->mutex);
            pthread_join(inst->audio_thread, nullptr);
            inst->audio_thread = 0;
        }
#endif
        inst->loader->deactivate();
    }
    ipc_write_ok(g_pipe_out);
}
