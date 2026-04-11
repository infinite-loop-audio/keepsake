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
#else
#include <windows.h>
#include <process.h>
#define getpid _getpid
#endif

// Bridge I/O pipes
#ifdef _WIN32
static PlatformPipe g_pipe_in  = GetStdHandle(STD_INPUT_HANDLE);
static PlatformPipe g_pipe_out = GetStdHandle(STD_OUTPUT_HANDLE);
#else
static PlatformPipe g_pipe_in  = STDIN_FILENO;
static PlatformPipe g_pipe_out = STDOUT_FILENO;
#endif

// --- Per-instance state ---

struct PluginInstance {
    uint32_t id;
    BridgeLoader *loader = nullptr;
    PlatformShm shm;
    int32_t num_inputs = 0;
    int32_t num_outputs = 0;
    uint32_t max_frames = 0;
    bool active = false;
    bool processing = false;
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

    if (!loader->load(path)) {
        ipc_write_error(g_pipe_out, "failed to load plugin");
        delete loader;
        return;
    }

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

int main(int /*argc*/, char * /*argv*/[]) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    gui_init();

    fprintf(stderr, "bridge: started (pid=%d)\n", getpid());

    while (true) {
        int read_timeout = gui_is_open() ? 16 : -1;

        uint32_t opcode;
        std::vector<uint8_t> payload;

        if (!ipc_read_msg(g_pipe_in, opcode, payload, read_timeout)) {
            if (gui_is_open()) {
                gui_idle(nullptr) /* TODO: pass active editor's loader */;
                continue;
            }
            fprintf(stderr, "bridge: pipe closed, exiting\n");
            break;
        }

        if (gui_is_open()) gui_idle(nullptr) /* TODO: pass active editor's loader */;

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
        case IPC_OP_PROCESS:    handle_process(inst, payload); break;
        case IPC_OP_SET_PARAM:  handle_set_param(inst, payload); break;
        case IPC_OP_MIDI_EVENT: handle_midi_event(inst, payload); break;
        case IPC_OP_GET_PARAM_INFO: handle_get_param_info(inst, payload); break;
        case IPC_OP_GET_CHUNK:  handle_get_chunk(inst); break;
        case IPC_OP_SET_CHUNK:  handle_set_chunk(inst, payload); break;
        case IPC_OP_EDITOR_OPEN:
            if (inst->loader && inst->loader->has_editor()) {
                // Build header info from payload if provided, else defaults
                EditorHeaderInfo hdr;
                hdr.plugin_name = "Plugin"; // could extract from loader
                // Try to parse header info from payload
                if (payload.size() > 0) {
                    // payload: [name_len][name][format][arch][isolation]
                    // Simple: just pass format string for now
                }
                // Determine format badge
                hdr.format = "VST2"; // TODO: pass from host
                hdr.architecture = "native";
                hdr.isolation = "shared";

                // Extract name from loader
                IpcPluginInfo pi = {};
                std::vector<uint8_t> extra;
                inst->loader->get_info(pi, extra);
                if (!extra.empty()) {
                    hdr.plugin_name = std::string(
                        reinterpret_cast<const char *>(extra.data()));
                }

                if (gui_open_editor(inst->loader, hdr))
                    ipc_write_ok(g_pipe_out);
                else
                    ipc_write_error(g_pipe_out, "EDITOR_OPEN: failed");
            } else {
                ipc_write_error(g_pipe_out, "EDITOR_OPEN: no editor");
            }
            break;
        case IPC_OP_EDITOR_CLOSE:
            ipc_write_ok(g_pipe_out);
            break;
        case IPC_OP_EDITOR_GET_RECT: handle_editor_get_rect(inst); break;
        case IPC_OP_STOP_PROC:
            inst->processing = false;
            ipc_write_ok(g_pipe_out);
            break;
        case IPC_OP_DEACTIVATE:
            if (inst->loader && inst->active) {
                inst->loader->deactivate();
                inst->active = false;
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
