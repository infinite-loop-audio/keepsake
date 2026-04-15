#include "vst2_loader_internal.h"

#include "ipc.h"
#include "platform.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

bool vst2_load_metadata_via_bridge(const std::string &path,
                                   const std::string &bridge_binary,
                                   Vst2PluginInfo &info) {
    PlatformProcess proc;
    if (!platform_spawn(bridge_binary, proc)) {
        fprintf(stderr, "keepsake: failed to spawn bridge '%s' for scan\n",
                bridge_binary.c_str());
        return false;
    }

    uint32_t fmt = FORMAT_VST2;
    std::vector<uint8_t> init_payload(4 + path.size());
    memcpy(init_payload.data(), &fmt, 4);
    memcpy(init_payload.data() + 4, path.data(), path.size());
    ipc_write_instance_msg(proc.pipe_to, IPC_OP_INIT, 0,
                           init_payload.data(),
                           static_cast<uint32_t>(init_payload.size()));

    uint32_t op;
    std::vector<uint8_t> payload;
    if (!ipc_read_msg(proc.pipe_from, op, payload, 15000) || op != IPC_OP_OK) {
        fprintf(stderr, "keepsake: bridge scan failed for '%s'\n", path.c_str());
        platform_force_kill(proc);
        return false;
    }

    if (payload.size() >= 4 + sizeof(IpcPluginInfo)) {
        size_t off = 4;
        IpcPluginInfo pi;
        memcpy(&pi, payload.data() + off, sizeof(pi));
        info.unique_id = pi.unique_id;
        info.num_inputs = pi.num_inputs;
        info.num_outputs = pi.num_outputs;
        info.num_params = pi.num_params;
        info.flags = pi.flags;
        info.category = pi.category;
        info.vendor_version = pi.vendor_version;

        off += sizeof(IpcPluginInfo);
        auto read_str = [&]() -> std::string {
            if (off >= payload.size()) return {};
            const char *s = reinterpret_cast<const char *>(payload.data() + off);
            size_t max_len = payload.size() - off;
            size_t len = strnlen(s, max_len);
            off += len + 1;
            return std::string(s, len);
        };
        info.name = read_str();
        info.vendor = read_str();
        info.product = read_str();
    }

    if (!vst2_bridge_info_is_sane(info)) {
        fprintf(stderr,
                "keepsake: rejecting corrupt bridge scan metadata for '%s' (category=%d in=%d out=%d params=%d)\n",
                path.c_str(), info.category, info.num_inputs,
                info.num_outputs, info.num_params);
        platform_force_kill(proc);
        return false;
    }

    info.file_path = path;
    info.needs_cross_arch = true;
    info.binary_arch = vst2_detect_binary_arch(path);
    if (info.name.empty()) info.name = vst2_filename_stem(path);
    if (info.vendor.empty()) info.vendor = "Unknown";

    platform_force_kill(proc);

    fprintf(stderr, "keepsake: scanned via bridge '%s' — name='%s' in=%d out=%d\n",
            path.c_str(), info.name.c_str(), info.num_inputs, info.num_outputs);

    return true;
}

bool scan_plugin_via_bridge(const std::string &path,
                            const std::string &bridge_binary,
                            uint32_t format,
                            Vst2PluginInfo &info) {
    PlatformProcess proc;
    if (!platform_spawn(bridge_binary, proc)) {
        fprintf(stderr, "keepsake: failed to spawn scanner bridge\n");
        return false;
    }

    std::vector<uint8_t> init_payload(4 + path.size());
    memcpy(init_payload.data(), &format, 4);
    memcpy(init_payload.data() + 4, path.data(), path.size());

    ipc_write_instance_msg(proc.pipe_to, IPC_OP_INIT, 0,
                           init_payload.data(),
                           static_cast<uint32_t>(init_payload.size()));

    uint32_t op;
    std::vector<uint8_t> payload;
    if (!ipc_read_msg(proc.pipe_from, op, payload, 15000)) {
        fprintf(stderr, "keepsake: scan timeout for '%s'\n", path.c_str());
        platform_force_kill(proc);
        return false;
    }

    if (op == IPC_OP_ERROR) {
        std::string msg(payload.begin(), payload.end());
        fprintf(stderr, "keepsake: scan error for '%s': %s\n",
                path.c_str(), msg.c_str());
        platform_force_kill(proc);
        return false;
    }

    if (op != IPC_OP_OK || !vst2_parse_init_response(payload, info)) {
        fprintf(stderr, "keepsake: scan failed for '%s' (op=0x%02X)\n",
                path.c_str(), op);
        platform_force_kill(proc);
        return false;
    }

    if (!vst2_bridge_info_is_sane(info)) {
        fprintf(stderr,
                "keepsake: rejecting corrupt scan metadata for '%s' (category=%d in=%d out=%d params=%d)\n",
                path.c_str(), info.category, info.num_inputs,
                info.num_outputs, info.num_params);
        platform_force_kill(proc);
        return false;
    }

    info.file_path = path;
    info.format = format;
    info.binary_arch = vst2_detect_binary_arch(path);
    if (info.name.empty()) info.name = vst2_filename_stem(path);
    if (info.vendor.empty()) info.vendor = "Unknown";

    platform_force_kill(proc);

    fprintf(stderr, "keepsake: scanned '%s' — name='%s' vendor='%s' format=%u\n",
            path.c_str(), info.name.c_str(), info.vendor.c_str(), format);
    return true;
}
