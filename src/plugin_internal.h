#pragma once
//
// Internal helpers shared across plugin_*.cpp files.
//

#include "plugin.h"
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

#ifndef _WIN32
#include <sched.h>
#endif

// Get the KeepsakePlugin from a clap_plugin_t pointer.
inline KeepsakePlugin *get(const clap_plugin_t *plugin) {
    return reinterpret_cast<KeepsakePlugin *>(plugin->plugin_data);
}

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

    ~AsyncBridgeInitState();
};

// Send a command and wait for OK/ERROR response.
bool send_and_wait(KeepsakePlugin *kp, uint32_t opcode,
                    const void *payload = nullptr,
                    uint32_t size = 0,
                    std::vector<uint8_t> *ok_payload = nullptr,
                    int timeout_ms = 3000);

// Bridge-level IPC helper used by async and synchronous paths.
bool send_and_wait_bridge(BridgeProcess *bridge,
                          uint32_t instance_id,
                          uint32_t opcode,
                          const void *payload = nullptr,
                          uint32_t size = 0,
                          std::vector<uint8_t> *ok_payload = nullptr,
                          int timeout_ms = 3000);

// Pull async bridge-init results into the live plugin instance once ready.
void sync_async_init(KeepsakePlugin *kp);

// Wait briefly for async bridge-init to finish when a host call depends on it.
bool wait_async_init(KeepsakePlugin *kp, int timeout_ms);

// Launch bridge init on a worker thread.
void launch_async_init(KeepsakePlugin *kp);

// Set the shared bridge pool used by plugin instances.
void keepsake_plugin_set_pool(BridgePool *pool);

// Queue or clear deferred activation work while async init is still running.
void queue_async_activation(KeepsakePlugin *kp,
                            const std::string &shm_name,
                            uint32_t shm_size,
                            double sample_rate,
                            uint32_t max_frames,
                            bool want_start_processing);
void clear_async_queue(KeepsakePlugin *kp, bool clear_activate);

// CLAP callback helpers split across plugin implementation files.
clap_process_status plugin_process(const clap_plugin_t *plugin,
                                   const clap_process_t *process);
const void *plugin_get_extension(const clap_plugin_t *plugin, const char *id);
void plugin_on_main_thread(const clap_plugin_t *plugin);

// Shared bridge pool accessor for plugin implementation files.
BridgePool *keepsake_plugin_pool();
