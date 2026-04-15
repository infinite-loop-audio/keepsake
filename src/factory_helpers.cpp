#include "factory_internal.h"

#include <unordered_map>
#include <unordered_set>
#include <cstdio>

std::string format_version(int32_t v) {
    if (v <= 0) return "0.0.0";

    char buf[32];

    int hex_major = (v >> 16) & 0xFF;
    int hex_minor = (v >> 8) & 0xFF;
    int hex_patch = v & 0xFF;

    if (hex_major >= 1 && hex_major < 100 &&
        hex_minor < 100 && hex_patch < 100 && v > 0xFFFF) {
        snprintf(buf, sizeof(buf), "%d.%d.%d", hex_major, hex_minor, hex_patch);
        return buf;
    }

    if (v >= 1000) {
        int dec_major = v / 1000;
        int dec_minor = (v / 100) % 10;
        int dec_patch = v % 100;
        snprintf(buf, sizeof(buf), "%d.%d.%d", dec_major, dec_minor, dec_patch);
        return buf;
    }

    snprintf(buf, sizeof(buf), "%d", v);
    return buf;
}

namespace {

const char *format_prefix(uint32_t format) {
    switch (format) {
    case FORMAT_VST2: return "vst2";
    case FORMAT_VST3: return "vst3";
    case FORMAT_AU:   return "au";
    default:          return "unknown";
    }
}

std::string make_plugin_id(uint32_t format, int32_t unique_id) {
    char buf[64];
    snprintf(buf, sizeof(buf), "keepsake.%s.%08X",
             format_prefix(format), static_cast<uint32_t>(unique_id));
    return buf;
}

std::string display_arch_suffix(const std::string &arch) {
    if (arch == "x86") return "x86";
    if (arch == "x86_64" || arch == "native") return "x64";
    if (arch == "arm64") return "arm64";
    return {};
}

std::string id_arch_suffix(const std::string &arch) {
    if (arch == "x86") return "x86";
    if (arch == "arm64") return "arm64";
    return {};
}

std::string make_plugin_id_disambiguated(uint32_t format,
                                         int32_t unique_id,
                                         const std::string &arch,
                                         const std::string &path,
                                         bool needs_suffix) {
    std::string base = make_plugin_id(format, unique_id);
    std::string arch_suffix = id_arch_suffix(arch);
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

void map_features(const Vst2PluginInfo &info, PluginEntry &entry) {
    bool is_synth = (info.flags & effFlagsIsSynth) != 0;

    if (is_synth) {
        entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_INSTRUMENT);
    }

    switch (info.category) {
    case kPlugCategSynth:
        if (!is_synth) entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_INSTRUMENT);
        entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_SYNTHESIZER);
        break;
    case kPlugCategEffect:
        if (!is_synth) entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_AUDIO_EFFECT);
        break;
    case kPlugCategAnalysis:
        entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_AUDIO_EFFECT);
        entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_ANALYZER);
        break;
    case kPlugCategMastering:
        entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_AUDIO_EFFECT);
        entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_MASTERING);
        break;
    case kPlugCategSpacializer:
        if (!is_synth) entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_AUDIO_EFFECT);
        break;
    case kPlugCategRoomFx:
        entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_AUDIO_EFFECT);
        entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_REVERB);
        break;
    case kPlugSurroundFx:
        entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_AUDIO_EFFECT);
        entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_SURROUND);
        break;
    case kPlugCategRestoration:
        entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_AUDIO_EFFECT);
        entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_RESTORATION);
        break;
    case kPlugCategGenerator:
        if (!is_synth) entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_INSTRUMENT);
        break;
    case kPlugCategUnknown:
    default:
        if (!is_synth) entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_AUDIO_EFFECT);
        break;
    }

    entry.features_ptrs.clear();
    for (const auto &f : entry.feature_strings) {
        entry.features_ptrs.push_back(f.c_str());
    }
    entry.features_ptrs.push_back(nullptr);
}

bool glob_match_simple(const std::string &pattern, const std::string &text) {
    if (pattern == text) return true;
    if (pattern == "*") return true;
    if (pattern.size() > 1 && pattern[0] == '*') {
        std::string suffix = pattern.substr(1);
        return text.size() >= suffix.size() &&
               text.substr(text.size() - suffix.size()) == suffix;
    }
    if (pattern.size() > 2 && pattern[0] == '*' && pattern.back() == '*') {
        std::string needle = pattern.substr(1, pattern.size() - 2);
        return text.find(needle) != std::string::npos;
    }
    return false;
}

} // namespace

void filter_plugins(std::vector<Vst2PluginInfo> &plugins,
                    const KeepsakeConfig &cfg) {
    if (cfg.expose_mode == "all") return;

    std::vector<Vst2PluginInfo> filtered;
    for (auto &p : plugins) {
        bool include = false;

        if (cfg.expose_mode == "auto") {
            include = p.needs_cross_arch;
        } else if (cfg.expose_mode == "whitelist") {
            for (const auto &wl : cfg.whitelist) {
                if (glob_match_simple(wl.path, p.file_path)) {
                    include = true;
                    break;
                }
            }
        }

        if (include) filtered.push_back(std::move(p));
    }

    size_t removed = plugins.size() - filtered.size();
    plugins = std::move(filtered);
    if (removed > 0) {
        fprintf(stderr, "keepsake: filtered to %zu plugins (removed %zu, mode=%s)\n",
                plugins.size(), removed, cfg.expose_mode.c_str());
    }
}

void build_descriptors(std::vector<Vst2PluginInfo> &plugins) {
    std::unordered_map<std::string, size_t> id_counts;
    std::unordered_set<std::string> assigned_ids;
    for (const auto &p : plugins) {
        std::string id_suffix = id_arch_suffix(p.binary_arch);
        std::string key = make_plugin_id(p.format, p.unique_id);
        if (p.format == FORMAT_VST2 && !id_suffix.empty()) {
            key += "." + id_suffix;
        }
        ++id_counts[key];
    }

    s_entries.clear();
    s_entries.reserve(plugins.size());

    for (auto &p : plugins) {
        PluginEntry entry;
        std::string arch_suffix = display_arch_suffix(p.binary_arch);
        std::string id_suffix = id_arch_suffix(p.binary_arch);
        std::string base_id = make_plugin_id(p.format, p.unique_id);
        if (p.format == FORMAT_VST2 && !id_suffix.empty()) {
            base_id += "." + id_suffix;
        }
        bool needs_suffix = id_counts[base_id] > 1;
        entry.id = make_plugin_id_disambiguated(
            p.format, p.unique_id, p.binary_arch, p.file_path, needs_suffix);
        if (!assigned_ids.insert(entry.id).second) {
            uint32_t hash = 0;
            for (char c : p.file_path) hash = hash * 31 + static_cast<uint32_t>(c);
            char suffix[8];
            snprintf(suffix, sizeof(suffix), ".%04x", hash & 0xFFFF);
            entry.id += suffix;
            assigned_ids.insert(entry.id);
        }
        entry.name = p.name;
        if (p.format == FORMAT_VST2 && !arch_suffix.empty()) {
            entry.name += (p.binary_arch == "x86") ? " (32-bit)" :
                          (p.binary_arch == "x86_64" || p.binary_arch == "native") ? " (64-bit)" :
                          (" (" + p.binary_arch + ")");
        }
        entry.vendor = p.vendor;
        entry.version_str = format_version(p.vendor_version);
        entry.plugin_path = p.file_path;
        entry.num_inputs = p.num_inputs;
        entry.num_outputs = p.num_outputs;
        entry.num_params = p.num_params;
        entry.has_editor = (p.flags & effFlagsHasEditor) != 0;
        entry.format = p.format;
        entry.binary_arch = p.binary_arch;
        entry.needs_32bit_bridge = (p.binary_arch == "x86");
        entry.needs_x86_64_bridge = (p.binary_arch == "x86_64") && p.needs_cross_arch;

        map_features(p, entry);

        entry.descriptor = {};
        entry.descriptor.clap_version = CLAP_VERSION;
        entry.descriptor.id = entry.id.c_str();
        entry.descriptor.name = entry.name.c_str();
        entry.descriptor.vendor = entry.vendor.c_str();
        entry.descriptor.url = nullptr;
        entry.descriptor.manual_url = nullptr;
        entry.descriptor.support_url = nullptr;
        entry.descriptor.version = entry.version_str.c_str();
        entry.descriptor.description = nullptr;
        entry.descriptor.features = entry.features_ptrs.data();

        s_entries.push_back(std::move(entry));
    }

    for (auto &e : s_entries) {
        e.descriptor.id = e.id.c_str();
        e.descriptor.name = e.name.c_str();
        e.descriptor.vendor = e.vendor.c_str();
        e.descriptor.version = e.version_str.c_str();

        e.features_ptrs.clear();
        for (const auto &f : e.feature_strings) {
            e.features_ptrs.push_back(f.c_str());
        }
        e.features_ptrs.push_back(nullptr);
        e.descriptor.features = e.features_ptrs.data();
    }
}
