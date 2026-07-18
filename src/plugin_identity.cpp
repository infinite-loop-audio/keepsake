#include "plugin_identity.h"

#include "ipc.h"

#include <cstdio>

namespace {

const char *format_prefix(uint32_t format) {
    switch (format) {
    case FORMAT_VST2: return "vst2";
    case FORMAT_VST3: return "vst3";
    case FORMAT_AU:   return "au";
    default:          return "unknown";
    }
}

} // namespace

std::string keepsake_make_plugin_id(uint32_t format, int32_t unique_id) {
    char buf[64];
    snprintf(buf, sizeof(buf), "keepsake.%s.%08X",
             format_prefix(format), static_cast<uint32_t>(unique_id));
    return buf;
}

std::string keepsake_plugin_id_arch_suffix(const std::string &arch) {
    if (arch == "x86") return "x86";
    if (arch == "arm64") return "arm64";
    return {};
}

std::string keepsake_make_plugin_id_disambiguated(uint32_t format,
                                                   int32_t unique_id,
                                                   const std::string &arch,
                                                   const std::string &path,
                                                   bool needs_suffix) {
    std::string base = keepsake_make_plugin_id(format, unique_id);
    std::string arch_suffix = keepsake_plugin_id_arch_suffix(arch);
    if (format == FORMAT_VST2 && !arch_suffix.empty()) {
        base += "." + arch_suffix;
        if (!needs_suffix) return base;
    } else if (!needs_suffix) {
        return base;
    }

    uint32_t hash = 0;
    for (char c : path) hash = hash * 31 + static_cast<uint32_t>(c);

    char suffix[8];
    snprintf(suffix, sizeof(suffix), ".%04x", hash & 0xFFFF);
    return base + suffix;
}
