//
// keepsake-bridge — multi-instance plugin server subprocess.
// Hosts one or more plugin instances, dispatched by instance ID.
//
// Contract refs:
//   docs/contracts/004-ipc-bridge-protocol.md
//   docs/contracts/006-process-isolation-policy.md
//

#include "ipc.h"
#include "bridge_loader.h"
#include "bridge_gui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>

#ifndef _WIN32
#include <unistd.h>
#include <poll.h>
#else
#include <windows.h>
#include <process.h>
#define getpid _getpid
#endif

// Bridge I/O: command pipe (stdin/stdout) + wake pipe (fd 3)
#ifdef _WIN32
static PlatformPipe g_pipe_in  = GetStdHandle(STD_INPUT_HANDLE);
static PlatformPipe g_pipe_out = GetStdHandle(STD_OUTPUT_HANDLE);
static PlatformPipe g_wake_fd  = PLATFORM_INVALID_PIPE;
#else
static PlatformPipe g_pipe_in  = STDIN_FILENO;
static PlatformPipe g_pipe_out = STDOUT_FILENO;
static PlatformPipe g_wake_fd  = 3; // wake pipe from parent, duped to fd 3
#endif

// --- Per-instance state ---

struct PluginInstance {
    uint32_t id;
    BridgeLoader *loader = nullptr;
    PlatformShm shm;
    int32_t num_inputs = 0;
    int32_t num_outputs = 0;
    uint32_t max_frames = 0;
    volatile bool active = false;
    bool processing = false;
#ifndef _WIN32
    pthread_t audio_thread = 0;
#endif
};

static std::unordered_map<uint32_t, PluginInstance *> g_instances;
static uint32_t g_next_instance_id = 1;

static PluginInstance *get_instance(uint32_t id) {
    auto it = g_instances.find(id);
    return (it != g_instances.end()) ? it->second : nullptr;
}

static void destroy_instance(uint32_t id) {
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

static void destroy_all_instances() {
    std::vector<uint32_t> ids;
    for (auto &kv : g_instances) ids.push_back(kv.first);
    for (auto id : ids) destroy_instance(id);
}

// --- Audio processing thread ---
// One thread per active instance. Blocks on pthread_cond_wait in shared
// memory until the host signals PROCESS_REQUESTED.

#ifndef _WIN32
static void *audio_thread_func(void *arg) {
    auto *inst = static_cast<PluginInstance *>(arg);
    auto *ctrl = shm_control(inst->shm.ptr);

    // Pre-allocate channel pointers
    std::vector<float *> in_ptrs(static_cast<size_t>(inst->num_inputs));
    std::vector<float *> out_ptrs(static_cast<size_t>(inst->num_outputs));

    fprintf(stderr, "bridge: audio thread started for instance %u\n", inst->id);

    while (inst->active) {
        pthread_mutex_lock(&ctrl->mutex);

        // Block until host signals PROCESS_REQUESTED
        while (ctrl->state != SHM_STATE_PROCESS_REQUESTED && inst->active) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 50000000; // 50ms timeout (check active flag)
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

        // Process audio
        uint32_t nframes = ctrl->num_frames;
        if (nframes > inst->max_frames) nframes = inst->max_frames;

        for (uint32_t p = 0; p < ctrl->param_count && p < 64; p++)
            inst->loader->set_param(ctrl->params[p].index,
                                     ctrl->params[p].value);
        for (uint32_t m = 0; m < ctrl->midi_count && m < SHM_MAX_MIDI_EVENTS; m++)
            inst->loader->send_midi(ctrl->midi_events[m].delta_frames,
                                     ctrl->midi_events[m].data);

        for (int i = 0; i < inst->num_inputs; i++)
            in_ptrs[static_cast<size_t>(i)] =
                shm_audio_inputs(inst->shm.ptr, i, inst->max_frames);
        for (int i = 0; i < inst->num_outputs; i++)
            out_ptrs[static_cast<size_t>(i)] =
                shm_audio_outputs(inst->shm.ptr, inst->num_inputs,
                                   i, inst->max_frames);

        inst->loader->process(
            inst->num_inputs > 0 ? in_ptrs.data() : nullptr,
            inst->num_inputs,
            inst->num_outputs > 0 ? out_ptrs.data() : nullptr,
            inst->num_outputs, nframes);

        ctrl->state = SHM_STATE_PROCESS_DONE;
        pthread_cond_signal(&ctrl->cond);
        pthread_mutex_unlock(&ctrl->mutex);
    }

    fprintf(stderr, "bridge: audio thread stopped for instance %u\n", inst->id);
    return nullptr;
}
#endif

// --- Command handlers (instance-aware) ---

static void handle_init(uint32_t /*caller_id*/, const std::vector<uint8_t> &payload) {
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

    // Load the plugin on a thread with a 5-second timeout.
    // Some plugins hang during VSTPluginMain or effOpen (license checks, etc.)
#ifndef _WIN32
    volatile bool load_done = false;
    volatile bool load_ok = false;

    pthread_t lt;
    struct LoadCtx { BridgeLoader *l; std::string p; volatile bool *done; volatile bool *ok; };
    auto *lctx = new LoadCtx{loader, path, &load_done, &load_ok};

    pthread_create(&lt, nullptr, [](void *arg) -> void * {
        auto *c = static_cast<LoadCtx *>(arg);
        *c->ok = c->l->load(c->p);
        *c->done = true;
        delete c;
        return nullptr;
    }, lctx);

    // Wait up to 5 seconds
    for (int i = 0; i < 50 && !load_done; i++) {
        usleep(100000); // 100ms
    }

    if (!load_done) {
        fprintf(stderr, "bridge: plugin load timed out (5s): %s\n", path.c_str());
        pthread_detach(lt); // let it die eventually
        ipc_write_error(g_pipe_out, "plugin load timed out");
        // Don't delete loader — thread still using it
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

    // Create instance
    auto *inst = new PluginInstance();
    inst->id = g_next_instance_id++;
    inst->loader = loader;

    IpcPluginInfo info = {};
    std::vector<uint8_t> extra;
    loader->get_info(info, extra);
    inst->num_inputs = info.num_inputs;
    inst->num_outputs = info.num_outputs;

    g_instances[inst->id] = inst;

    // Response: [instance_id][IpcPluginInfo][extra strings]
    size_t total = 4 + sizeof(info) + extra.size();
    std::vector<uint8_t> resp(total);
    memcpy(resp.data(), &inst->id, 4);
    memcpy(resp.data() + 4, &info, sizeof(info));
    if (!extra.empty())
        memcpy(resp.data() + 4 + sizeof(info), extra.data(), extra.size());

    ipc_write_ok(g_pipe_out, resp.data(), static_cast<uint32_t>(total));
    fprintf(stderr, "bridge: created instance %u for '%s'\n",
            inst->id, path.c_str());
}

static void handle_set_shm(PluginInstance *inst, const std::vector<uint8_t> &payload) {
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

static void handle_activate(PluginInstance *inst, const std::vector<uint8_t> &payload) {
    if (!inst->loader || payload.size() < sizeof(IpcActivatePayload)) {
        ipc_write_error(g_pipe_out, "ACTIVATE: not ready");
        return;
    }
    IpcActivatePayload ap;
    memcpy(&ap, payload.data(), sizeof(ap));
    inst->max_frames = ap.max_frames;
    inst->loader->activate(ap.sample_rate, ap.max_frames);
    inst->active = true;

    // Start audio processing thread
#ifndef _WIN32
    if (inst->shm.ptr) {
        pthread_create(&inst->audio_thread, nullptr, audio_thread_func, inst);
    }
#endif

    ipc_write_ok(g_pipe_out);
}

static void handle_process(PluginInstance *inst, const std::vector<uint8_t> &payload) {
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
    for (int i = 0; i < inst->num_inputs; i++)
        in_ptrs[static_cast<size_t>(i)] = base + i * inst->max_frames;
    for (int i = 0; i < inst->num_outputs; i++)
        out_ptrs[static_cast<size_t>(i)] =
            base + (inst->num_inputs + i) * inst->max_frames;

    inst->loader->process(
        inst->num_inputs > 0 ? in_ptrs.data() : nullptr, inst->num_inputs,
        inst->num_outputs > 0 ? out_ptrs.data() : nullptr, inst->num_outputs,
        frames);

    ipc_write_process_done(g_pipe_out);
}

static void handle_set_param(PluginInstance *inst, const std::vector<uint8_t> &payload) {
    if (!inst->loader || payload.size() < sizeof(IpcSetParamPayload)) return;
    IpcSetParamPayload sp;
    memcpy(&sp, payload.data(), sizeof(sp));
    inst->loader->set_param(sp.index, sp.value);
}

static void handle_midi_event(PluginInstance *inst, const std::vector<uint8_t> &payload) {
    if (!inst->loader || payload.size() < sizeof(IpcMidiEventPayload)) return;
    IpcMidiEventPayload mp;
    memcpy(&mp, payload.data(), sizeof(mp));
    inst->loader->send_midi(mp.delta_frames, mp.data);
}

static void handle_get_param_info(PluginInstance *inst, const std::vector<uint8_t> &payload) {
    if (!inst->loader || payload.size() < 4) {
        ipc_write_error(g_pipe_out, "GET_PARAM_INFO: not ready");
        return;
    }
    uint32_t index;
    memcpy(&index, payload.data(), 4);
    IpcParamInfoResponse resp = {};
    if (inst->loader->get_param_info(index, resp))
        ipc_write_ok(g_pipe_out, &resp, sizeof(resp));
    else
        ipc_write_error(g_pipe_out, "GET_PARAM_INFO: index out of range");
}

static void handle_get_chunk(PluginInstance *inst) {
    if (!inst->loader) { ipc_write_error(g_pipe_out, "GET_CHUNK: not ready"); return; }
    auto chunk = inst->loader->get_chunk();
    ipc_write_ok(g_pipe_out, chunk.empty() ? nullptr : chunk.data(),
                  static_cast<uint32_t>(chunk.size()));
}

static void handle_set_chunk(PluginInstance *inst, const std::vector<uint8_t> &payload) {
    if (!inst->loader || payload.empty()) {
        ipc_write_error(g_pipe_out, "SET_CHUNK: not ready"); return;
    }
    inst->loader->set_chunk(payload.data(), payload.size());
    ipc_write_ok(g_pipe_out);
}

static void handle_editor_get_rect(PluginInstance *inst) {
    IpcEditorRect rect = {};
    int w = 0, h = 0;
    if (inst->loader && inst->loader->get_editor_rect(w, h)) {
        rect.width = w; rect.height = h;
    }
    ipc_write_ok(g_pipe_out, &rect, sizeof(rect));
}

// --- Main loop ---

int main(int argc, char *argv[]) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    gui_init();

    // Wake pipe fd passed as argv[1] by the host
    bool has_wake_pipe = false;
#ifndef _WIN32
    g_wake_fd = -1;
    if (argc > 1) {
        int fd = atoi(argv[1]);
        if (fd > 0) {
            struct pollfd test_pfd = { fd, POLLIN, 0 };
            int r = poll(&test_pfd, 1, 0);
            if (r >= 0 && !(test_pfd.revents & POLLNVAL)) {
                g_wake_fd = fd;
                has_wake_pipe = true;
            }
        }
    }
#endif

    fprintf(stderr, "bridge: started (pid=%d) wake=%s\n", getpid(),
            has_wake_pipe ? "pipe" : "poll");

    // Pre-allocated channel pointer arrays (avoid malloc in audio path)
    std::vector<float *> proc_in_ptrs;
    std::vector<float *> proc_out_ptrs;

    // GUI throttle — 60fps is plenty
    uint64_t last_gui_idle_us = 0;
    auto now_us = []() -> uint64_t {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000000 +
               static_cast<uint64_t>(ts.tv_nsec) / 1000;
    };

    // Audio processing runs on a dedicated thread per active instance.
    // The thread blocks on pthread_cond_wait (zero CPU) and wakes
    // instantly when the host signals. No polling, no pipes.
    //
    // The main thread handles: pipe commands + GUI only.

    while (true) {
        // GUI throttle
        if (gui_is_open()) {
            uint64_t now = now_us();
            if (now - last_gui_idle_us >= 16000) {
                gui_idle(nullptr);
                last_gui_idle_us = now;
            }
        }

        // Command pipe (blocking when idle, short timeout when GUI open)
        int pipe_timeout = gui_is_open() ? 16 : -1;

#ifndef _WIN32
        struct pollfd pfd = { g_pipe_in, POLLIN, 0 };
        int pr = poll(&pfd, 1, pipe_timeout);
        if (pr <= 0) continue;
        if (!(pfd.revents & POLLIN)) continue;
#else
        if (!platform_read_ready(g_pipe_in, pipe_timeout)) continue;
#endif

        // Read IPC message from command pipe
        uint32_t opcode;
        std::vector<uint8_t> payload;
        if (!ipc_read_msg(g_pipe_in, opcode, payload)) {
            fprintf(stderr, "bridge: command pipe closed, exiting\n");
            break;
        }

        // Extract instance ID from payload (first 4 bytes)
        uint32_t instance_id = ipc_extract_instance_id(payload);

        // INIT is special — creates a new instance (instance_id from caller is 0)
        if (opcode == IPC_OP_INIT) {
            handle_init(instance_id, payload);
            continue;
        }

        // SHUTDOWN with instance_id 0 = exit entire process
        if (opcode == IPC_OP_SHUTDOWN && instance_id == 0) {
            ipc_write_ok(g_pipe_out);
            break;
        }

        // SHUTDOWN with specific instance = destroy just that instance
        if (opcode == IPC_OP_SHUTDOWN) {
            destroy_instance(instance_id);
            ipc_write_ok(g_pipe_out);
            fprintf(stderr, "bridge: destroyed instance %u (%zu remaining)\n",
                    instance_id, g_instances.size());
            continue;
        }

        // All other opcodes need a valid instance
        auto *inst = get_instance(instance_id);
        if (!inst) {
            ipc_write_error(g_pipe_out, "unknown instance");
            continue;
        }

        switch (opcode) {
        case IPC_OP_SET_SHM:    handle_set_shm(inst, payload); break;
        case IPC_OP_ACTIVATE:   handle_activate(inst, payload); break;
        case IPC_OP_START_PROC:
            inst->processing = true;
            ipc_write_ok(g_pipe_out);
            break;
        case IPC_OP_PROCESS:
            // Legacy pipe-based process (fallback, normally handled via shm)
            handle_process(inst, payload);
            break;
        case IPC_OP_SET_PARAM:  handle_set_param(inst, payload); break;
        case IPC_OP_MIDI_EVENT: handle_midi_event(inst, payload); break;
        case IPC_OP_GET_PARAM_INFO: handle_get_param_info(inst, payload); break;
        case IPC_OP_GET_CHUNK:  handle_get_chunk(inst); break;
        case IPC_OP_SET_CHUNK:  handle_set_chunk(inst, payload); break;
        case IPC_OP_EDITOR_OPEN:
            if (inst->loader && inst->loader->has_editor()) {
                EditorHeaderInfo hdr;
                hdr.format = "VST2";
#if defined(__x86_64__)
                hdr.architecture = "x86_64 (Rosetta)";
#elif defined(__aarch64__)
                hdr.architecture = "ARM64";
#elif defined(__i386__)
                hdr.architecture = "32-bit";
#else
                hdr.architecture = "native";
#endif
                hdr.isolation = "per-instance";
                IpcPluginInfo pi2 = {};
                std::vector<uint8_t> extra2;
                inst->loader->get_info(pi2, extra2);
                if (!extra2.empty())
                    hdr.plugin_name = std::string(
                        reinterpret_cast<const char *>(extra2.data()));
                else
                    hdr.plugin_name = "Plugin";

                if (gui_open_editor(inst->loader, hdr))
                    ipc_write_ok(g_pipe_out);
                else
                    ipc_write_error(g_pipe_out, "EDITOR_OPEN: failed");
            } else {
                ipc_write_error(g_pipe_out, "EDITOR_OPEN: no editor");
            }
            break;
        case IPC_OP_EDITOR_CLOSE:
            gui_close_editor(inst->loader);
            ipc_write_ok(g_pipe_out);
            break;
        case IPC_OP_EDITOR_MOUSE:
            if (gui_is_iosurface_mode() && payload.size() >= sizeof(IpcMouseEvent)) {
                IpcMouseEvent mev;
                memcpy(&mev, payload.data(), sizeof(mev));
                gui_forward_mouse(mev);
            }
            break;
        case IPC_OP_EDITOR_KEY:
            if (gui_is_iosurface_mode() && payload.size() >= sizeof(IpcKeyEvent)) {
                IpcKeyEvent kev;
                memcpy(&kev, payload.data(), sizeof(kev));
                gui_forward_key(kev);
            }
            break;
        case IPC_OP_EDITOR_GET_RECT: handle_editor_get_rect(inst); break;
        case IPC_OP_EDITOR_SET_PARENT:
            if (inst->loader && payload.size() >= 8) {
                uint64_t handle;
                memcpy(&handle, payload.data(), 8);
                if (gui_open_editor_embedded(inst->loader, handle))
                    ipc_write_ok(g_pipe_out);
                else
                    ipc_write_error(g_pipe_out, "EDITOR_SET_PARENT: failed");
            } else {
                ipc_write_error(g_pipe_out, "EDITOR_SET_PARENT: not ready");
            }
            break;
        case IPC_OP_STOP_PROC:
            inst->processing = false;
            ipc_write_ok(g_pipe_out);
            break;
        case IPC_OP_DEACTIVATE:
            if (inst->loader && inst->active) {
                inst->active = false; // signals audio thread to stop
#ifndef _WIN32
                if (inst->shm.ptr) {
                    // Wake the audio thread so it sees active=false
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
            break;
        default:
            ipc_write_error(g_pipe_out, "unknown opcode");
            break;
        }
    }

    // Cleanup all instances
    if (gui_is_open()) gui_close_editor(nullptr) /* TODO: pass active editor's loader */;
    destroy_all_instances();

    fprintf(stderr, "bridge: exiting cleanly\n");
    return 0;
}
