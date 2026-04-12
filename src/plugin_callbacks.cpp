//
// Keepsake CLAP plugin callbacks.
//

#include "plugin_internal.h"

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
    if (kp->bridge && keepsake_plugin_pool()) {
        if (kp->crashed) keepsake_plugin_pool()->abandon(kp->bridge);
        else keepsake_plugin_pool()->release(kp->bridge);
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

    constexpr int32_t kMaxBridgeChannels = 64;
    constexpr size_t kMaxBridgeShmBytes = 64u * 1024u * 1024u;

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
    IsolationMode isolation) {
    keepsake_plugin_set_pool(pool);

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
