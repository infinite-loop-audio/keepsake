//
// Keepsake CLAP plugin — params extension.
//

#include "plugin_internal.h"

// Lazy-load parameter info from bridge on first access.
static void ensure_params_loaded(KeepsakePlugin *kp) {
    if (kp->params_loaded || kp->crashed || !kp->bridge_ok || !kp->active) return;
    kp->params_loaded = true;
    kp->params.resize(static_cast<size_t>(kp->num_params));
    for (int32_t i = 0; i < kp->num_params; i++) {
        uint32_t idx = static_cast<uint32_t>(i);
        std::vector<uint8_t> data;
        if (send_and_wait(kp, IPC_OP_GET_PARAM_INFO, &idx, sizeof(idx), &data) &&
            data.size() >= sizeof(IpcParamInfoResponse)) {
            IpcParamInfoResponse resp;
            memcpy(&resp, data.data(), sizeof(resp));
            auto &cp = kp->params[static_cast<size_t>(i)];
            cp.index = resp.index;
            cp.default_value = resp.current_value;
            memcpy(cp.name, resp.name, sizeof(cp.name));
            memcpy(cp.label, resp.label, sizeof(cp.label));
        }
    }
    fprintf(stderr, "keepsake: lazy-loaded %d param(s)\n", kp->num_params);
}

uint32_t keepsake_params_count(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    ensure_params_loaded(kp);
    // Return known count even if params aren't loaded yet
    if (kp->params.empty() && kp->num_params > 0)
        return static_cast<uint32_t>(kp->num_params);
    return static_cast<uint32_t>(kp->params.size());
}

bool keepsake_params_get_info(const clap_plugin_t *plugin, uint32_t index,
                               clap_param_info_t *info) {
    auto *kp = get(plugin);
    ensure_params_loaded(kp);
    if (index >= kp->params.size()) {
        // Params not loaded yet — return placeholder
        if (index < static_cast<uint32_t>(kp->num_params)) {
            memset(info, 0, sizeof(*info));
            info->id = static_cast<clap_id>(index);
            info->flags = CLAP_PARAM_IS_AUTOMATABLE;
            snprintf(info->name, sizeof(info->name), "Param %u", index);
            info->min_value = 0.0;
            info->max_value = 1.0;
            info->default_value = 0.5;
            return true;
        }
        return false;
    }
    const auto &cp = kp->params[index];

    memset(info, 0, sizeof(*info));
    info->id = static_cast<clap_id>(cp.index);
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    snprintf(info->name, sizeof(info->name), "%s", cp.name);
    info->module[0] = '\0';
    info->min_value = 0.0;
    info->max_value = 1.0;
    info->default_value = static_cast<double>(cp.default_value);
    return true;
}

bool keepsake_params_get_value(const clap_plugin_t *plugin, clap_id param_id,
                                double *value) {
    auto *kp = get(plugin);
    if (kp->crashed || !kp->bridge_ok) return false;
    if (param_id < kp->params.size()) {
        *value = static_cast<double>(kp->params[param_id].default_value);
        return true;
    }
    return false;
}

bool keepsake_params_value_to_text(const clap_plugin_t *plugin,
                                    clap_id param_id, double value,
                                    char *buf, uint32_t buf_size) {
    auto *kp = get(plugin);
    if (param_id >= kp->params.size()) return false;
    const auto &cp = kp->params[param_id];
    if (cp.label[0])
        snprintf(buf, buf_size, "%.2f %s", value, cp.label);
    else
        snprintf(buf, buf_size, "%.2f", value);
    return true;
}

bool keepsake_params_text_to_value(const clap_plugin_t *, clap_id,
                                    const char *text, double *value) {
    char *end = nullptr;
    double v = strtod(text, &end);
    if (end == text) return false;
    *value = v;
    return true;
}

void keepsake_params_flush(const clap_plugin_t *plugin,
                            const clap_input_events_t *in,
                            const clap_output_events_t *) {
    auto *kp = get(plugin);
    if (!in || kp->crashed || !kp->bridge) return;
    std::lock_guard<std::mutex> lock(kp->ipc_mutex);
    uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; i++) {
        auto *hdr = in->get(in, i);
        if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
            auto *pv = reinterpret_cast<const clap_event_param_value_t *>(hdr);
            IpcSetParamPayload sp;
            sp.index = static_cast<uint32_t>(pv->param_id);
            sp.value = static_cast<float>(pv->value);
            ipc_write_instance_msg(kp->bridge->proc.pipe_to,
                IPC_OP_SET_PARAM, kp->instance_id, &sp, sizeof(sp));
        }
    }
}

extern const clap_plugin_params_t keepsake_params_ext;
const clap_plugin_params_t keepsake_params_ext = {
    .count = keepsake_params_count,
    .get_info = keepsake_params_get_info,
    .get_value = keepsake_params_get_value,
    .value_to_text = keepsake_params_value_to_text,
    .text_to_value = keepsake_params_text_to_value,
    .flush = keepsake_params_flush,
};
