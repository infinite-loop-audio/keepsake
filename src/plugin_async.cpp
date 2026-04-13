//
// Keepsake CLAP plugin — async bridge init helpers.
//

#include "plugin_internal.h"
#include "debug_log.h"
#include <chrono>
#include <thread>

bool send_and_wait_bridge(BridgeProcess *bridge,
                          uint32_t instance_id,
                          uint32_t opcode,
                          const void *payload,
                          uint32_t size,
                          std::vector<uint8_t> *ok_payload,
                          int timeout_ms) {
    if (!bridge) return false;
    keepsake_debug_log("keepsake: bridge send opcode=0x%02X instance=%u timeout=%d\n",
                       opcode, instance_id, timeout_ms);
    if (!ipc_write_instance_msg(bridge->proc.pipe_to, opcode,
                                instance_id, payload, size))
        return false;

    uint32_t resp_op;
    std::vector<uint8_t> resp_payload;
    if (!ipc_read_msg(bridge->proc.pipe_from, resp_op, resp_payload, timeout_ms))
        return false;
    keepsake_debug_log("keepsake: bridge recv opcode=0x%02X instance=%u size=%zu\n",
                       resp_op, instance_id, resp_payload.size());
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

void request_host_refresh(const clap_host_t *host) {
    if (!host) return;
    if (host->request_restart) host->request_restart(host);
    if (host->request_callback) host->request_callback(host);
}

AsyncBridgeInitState::~AsyncBridgeInitState() {
    if (!bridge || !pool) return;
    if (success) pool->release(bridge);
    else pool->abandon(bridge);
    bridge = nullptr;
}

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

void launch_async_init(KeepsakePlugin *kp) {
    auto state = std::make_shared<AsyncBridgeInitState>();
    state->pool = keepsake_plugin_pool();
    state->host = kp->host;
    state->bridge_binary = kp->bridge_binary;
    state->plugin_path = kp->vst2_path;
    state->format = kp->format;
    state->isolation = kp->isolation;
    kp->async_init = state;
    keepsake_debug_log("keepsake: launch_async_init desc=%s path=%s format=%u isolation=%d\n",
                       kp->descriptor ? kp->descriptor->id : "(null)",
                       kp->vst2_path.c_str(),
                       kp->format,
                       static_cast<int>(kp->isolation));

    std::thread([state]() {
        keepsake_debug_log("keepsake: async thread acquire bridge path=%s format=%u isolation=%d\n",
                           state->plugin_path.c_str(),
                           state->format,
                           static_cast<int>(state->isolation));
        BridgeProcess *bridge = state->pool->acquire(
            state->bridge_binary, state->plugin_path, state->format,
            state->isolation);
        if (!bridge) {
            keepsake_debug_log("keepsake: async acquire bridge FAILED path=%s\n",
                               state->plugin_path.c_str());
            std::lock_guard<std::mutex> lock(state->mutex);
            state->completed = true;
            state->cv.notify_all();
            request_host_refresh(state->host);
            return;
        }
        keepsake_debug_log("keepsake: async acquire bridge OK pid=%d\n",
                           static_cast<int>(bridge->proc.pid));

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
            keepsake_debug_log("keepsake: async INIT OK instance=%u in=%d out=%d params=%d editor=%d\n",
                               instance_id, num_inputs, num_outputs, num_params,
                               has_editor ? 1 : 0);
        } else {
            keepsake_debug_log("keepsake: async INIT FAILED path=%s ok=%d payload=%zu\n",
                               state->plugin_path.c_str(), ok ? 1 : 0, ok_data.size());
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
                    keepsake_debug_log("keepsake: async SET_SHM OK instance=%u size=%u\n",
                                       instance_id, shm_size);
                    IpcActivatePayload ap = { sample_rate, max_frames };
                    ok = send_and_wait_bridge(bridge, instance_id, IPC_OP_ACTIVATE,
                                              &ap, sizeof(ap), nullptr, 3000);
                    if (ok) {
                        keepsake_debug_log("keepsake: async ACTIVATE OK instance=%u sr=%.1f max=%u\n",
                                           instance_id, sample_rate, max_frames);
                    }
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
                    keepsake_debug_log("keepsake: async START_PROC OK instance=%u\n",
                                       instance_id);
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
            keepsake_debug_log("keepsake: async init final FAILED path=%s\n",
                               state->plugin_path.c_str());
            bool owns_bridge = false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                owns_bridge = (state->bridge == bridge);
                if (owns_bridge) state->bridge = nullptr;
            }
            if (owns_bridge) state->pool->abandon(bridge);
        }
        keepsake_debug_log("keepsake: async init final completed success=%d instance=%u\n",
                           ok ? 1 : 0, instance_id);

        request_host_refresh(state->host);
    }).detach();
}
