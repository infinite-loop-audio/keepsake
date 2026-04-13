#pragma once
//
// Keepsake CLAP plugin instance — bridges a single VST2 plugin via
// a subprocess running keepsake-bridge.
//

#include "ipc.h"
#include "platform.h"
#include "bridge_pool.h"
#include <clap/clap.h>
#include <memory>
#include <string>
#include <vector>
#include <mutex>

struct AsyncBridgeInitState;

struct CachedParamInfo {
    uint32_t index;
    float default_value;
    char name[64];
    char label[16];
};

struct KeepsakePlugin {
    clap_plugin_t clap;
    const clap_host_t *host;

    // Identity (from factory scan)
    const clap_plugin_descriptor_t *descriptor;
    std::string vst2_path;
    std::string bridge_binary;
    uint32_t format = 0; // PluginFormat
    IsolationMode isolation = IsolationMode::SHARED;

    // Plugin metadata (confirmed by bridge after INIT)
    int32_t num_inputs = 0;
    int32_t num_outputs = 0;
    int32_t num_params = 0;

    // Bridge process (from pool)
    BridgeProcess *bridge = nullptr;
    uint32_t instance_id = 0; // assigned by bridge on INIT
    std::mutex ipc_mutex; // serializes command pipe I/O between threads
    int wake_pipe_w = -1;  // write end of wake pipe (host → bridge, audio signal only)

    // Shared memory
    PlatformShm shm;
    uint32_t max_frames = 0;

    // Cached parameter info (queried lazily on first access)
    std::vector<CachedParamInfo> params;
    bool params_loaded = false;

    // Editor
    bool has_editor = false;
    bool editor_open = false;
    bool gui_is_floating = true;
    bool gui_embed_failed = false;
    int32_t editor_width = 0;
    int32_t editor_height = 0;
    void *iosurface_layer = nullptr; // CALayer* for IOSurface refresh

    // State
    volatile bool bridge_ok = false;
    volatile bool active = false;
    bool processing = false;
    bool crashed = false;

    std::shared_ptr<AsyncBridgeInitState> async_init;
};

// Create a KeepsakePlugin. Returns a clap_plugin_t pointer (the plugin's
// first member) or nullptr on failure.
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
    IsolationMode isolation);
