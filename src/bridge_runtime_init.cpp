//
// keepsake-bridge runtime helpers — instance lifecycle and init/load path.
//

#include "bridge_runtime.h"
#include "bridge_gui.h"
#include "debug_log.h"

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

void handle_init(uint32_t /*caller_id*/, const std::vector<uint8_t> &payload) {
    if (payload.size() < 5) {
        ipc_write_error(g_pipe_out, "INIT payload too small");
        return;
    }

    uint32_t format_id;
    memcpy(&format_id, payload.data(), sizeof(format_id));
    std::string path(payload.begin() + 4, payload.end());
    keepsake_debug_log("bridge: INIT begin format=%u path=%s\n",
                       format_id, path.c_str());

    auto *loader = create_loader(static_cast<PluginFormat>(format_id));
    if (!loader) {
        keepsake_debug_log("bridge: INIT unsupported format=%u\n", format_id);
        ipc_write_error(g_pipe_out, "unsupported format");
        return;
    }

    bool load_on_main_thread = false;
#if defined(__APPLE__)
    load_on_main_thread = (format_id == FORMAT_VST2);
#endif

    if (load_on_main_thread) {
        keepsake_debug_log("bridge: INIT load on main thread path=%s\n", path.c_str());
        if (!loader->load(path)) {
            keepsake_debug_log("bridge: INIT load FAILED path=%s\n", path.c_str());
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
        keepsake_debug_log("bridge: INIT load direct path=%s\n", path.c_str());
        if (!loader->load(path)) {
            keepsake_debug_log("bridge: INIT load FAILED path=%s\n", path.c_str());
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
    keepsake_debug_log("bridge: INIT loaded path=%s in=%d out=%d params=%d flags=%d\n",
                       path.c_str(), info.num_inputs, info.num_outputs,
                       info.num_params, info.flags);

    g_instances[inst->id] = inst;

    size_t total = 4 + sizeof(info) + extra.size();
    std::vector<uint8_t> resp(total);
    memcpy(resp.data(), &inst->id, 4);
    memcpy(resp.data() + 4, &info, sizeof(info));
    if (!extra.empty()) {
        memcpy(resp.data() + 4 + sizeof(info), extra.data(), extra.size());
    }

    ipc_write_ok(g_pipe_out, resp.data(), static_cast<uint32_t>(total));
    keepsake_debug_log("bridge: INIT OK instance=%u path=%s\n",
                       inst->id, path.c_str());
    fprintf(stderr, "bridge: created instance %u for '%s'\n", inst->id, path.c_str());
}
