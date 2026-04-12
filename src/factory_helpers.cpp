#include "factory_internal.h"

#include <unordered_set>
#include <cstdlib>
#include <cstdio>
#include <cstring>

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

std::string make_plugin_id_disambiguated(uint32_t format,
                                         int32_t unique_id,
                                         const std::string &path,
                                         bool needs_suffix) {
    std::string base = make_plugin_id(format, unique_id);
    if (!needs_suffix) return base;

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

bool is_vst2_file(const fs::path &p) {
#ifdef __APPLE__
    return p.extension() == ".vst" && fs::is_directory(p);
#elif defined(_WIN32)
    return p.extension() == ".dll" && fs::is_regular_file(p);
#else
    return p.extension() == ".so" && fs::is_regular_file(p);
#endif
}

bool is_vst3_file(const fs::path &p) {
    return p.extension() == ".vst3";
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

void scan_vst2_entry(const std::string &entry_path,
                     std::vector<Vst2PluginInfo> &results,
                     bool targeted_vst2_override) {
    std::error_code ec;
    fs::path entry(entry_path);

    auto try_scan_plugin = [&](const fs::path &plugin_path) {
        Vst2PluginInfo info;
        info.format = FORMAT_VST2;
        if (vst2_load_metadata(plugin_path.string(), info)) {
            results.push_back(std::move(info));
            return;
        }
        if (targeted_vst2_override && !s_bridge_x86_64_path.empty()) {
            Vst2PluginInfo cross_info;
            cross_info.format = FORMAT_VST2;
            if (vst2_load_metadata_via_bridge(plugin_path.string(),
                                              s_bridge_x86_64_path,
                                              cross_info)) {
                cross_info.needs_cross_arch = true;
                results.push_back(std::move(cross_info));
            }
        }
    };

    if (is_vst2_file(entry)) {
        try_scan_plugin(entry);
        return;
    }

    if (!fs::is_directory(entry, ec)) return;
    for (const auto &dir_entry : fs::directory_iterator(entry, ec)) {
        if (ec) break;
        if (!is_vst2_file(dir_entry.path())) continue;
        try_scan_plugin(dir_entry.path());
    }
}

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>

void scan_au_plugins(std::vector<Vst2PluginInfo> &results) {
    AudioComponentDescription desc = {};
    desc.componentType = 0;
    AudioComponent comp = nullptr;

    while ((comp = AudioComponentFindNext(comp, &desc)) != nullptr) {
        AudioComponentDescription cd = {};
        AudioComponentGetDescription(comp, &cd);

        if (cd.componentType != kAudioUnitType_MusicDevice &&
            cd.componentType != kAudioUnitType_MusicEffect &&
            cd.componentType != kAudioUnitType_Effect) {
            continue;
        }

        auto cc = [](OSType t) -> std::string {
            char s[5];
            s[0] = static_cast<char>((t >> 24) & 0xFF);
            s[1] = static_cast<char>((t >> 16) & 0xFF);
            s[2] = static_cast<char>((t >> 8) & 0xFF);
            s[3] = static_cast<char>(t & 0xFF);
            s[4] = '\0';
            return s;
        };

        std::string au_path = cc(cd.componentType) + ":" +
                              cc(cd.componentSubType) + ":" +
                              cc(cd.componentManufacturer);

        CFStringRef cf_name = nullptr;
        AudioComponentCopyName(comp, &cf_name);

        Vst2PluginInfo info;
        info.format = FORMAT_AU;
        info.file_path = au_path;
        info.unique_id = static_cast<int32_t>(cd.componentSubType);
        info.flags = (cd.componentType == kAudioUnitType_MusicDevice) ? 0x100 : 0;
        info.category = (cd.componentType == kAudioUnitType_MusicDevice) ? 2 : 1;
        info.num_inputs = (cd.componentType == kAudioUnitType_MusicDevice) ? 0 : 2;
        info.num_outputs = 2;

        if (cf_name) {
            char name_buf[256] = {};
            CFStringGetCString(cf_name, name_buf, sizeof(name_buf),
                               kCFStringEncodingUTF8);
            CFRelease(cf_name);

            char *sep = strstr(name_buf, ": ");
            if (sep) {
                *sep = '\0';
                info.vendor = name_buf;
                info.name = sep + 2;
            } else {
                info.name = name_buf;
                info.vendor = "Unknown";
            }
        } else {
            info.name = au_path;
            info.vendor = "Unknown";
        }

        results.push_back(std::move(info));
    }

    fprintf(stderr, "keepsake: enumerated %zu AU plugin(s)\n", results.size());
}
#else
void scan_au_plugins(std::vector<Vst2PluginInfo> &) {}
#endif

void scan_vst3_directory(const std::string &dir_path,
                         std::vector<Vst2PluginInfo> &results) {
    std::error_code ec;
    if (!fs::is_directory(dir_path, ec)) return;

    for (const auto &dir_entry : fs::directory_iterator(dir_path, ec)) {
        if (ec) break;
        if (!is_vst3_file(dir_entry.path())) continue;

        Vst2PluginInfo info;
        if (scan_plugin_via_bridge(dir_entry.path().string(),
                                   s_bridge_path, FORMAT_VST3, info)) {
            results.push_back(std::move(info));
        }
    }
}

std::vector<std::string> get_scan_paths() {
    std::vector<std::string> paths;
    const char *env = getenv("KEEPSAKE_VST2_PATH");
    if (env && env[0] != '\0') {
#ifdef _WIN32
        char delim = ';';
#else
        char delim = ':';
#endif
        std::string s(env);
        size_t start = 0;
        while (start < s.size()) {
            size_t end = s.find(delim, start);
            if (end == std::string::npos) end = s.size();
            if (end > start) paths.push_back(s.substr(start, end - start));
            start = end + 1;
        }
        return paths;
    }

#ifdef __APPLE__
    const char *home = getenv("HOME");
    if (home) paths.push_back(std::string(home) + "/Library/Audio/Plug-Ins/VST");
    paths.push_back("/Library/Audio/Plug-Ins/VST");
#elif defined(_WIN32)
    const char *pf = getenv("PROGRAMFILES");
    if (pf) {
        paths.push_back(std::string(pf) + "\\VSTPlugins");
        paths.push_back(std::string(pf) + "\\Steinberg\\VSTPlugins");
    }
    const char *pf86 = getenv("PROGRAMFILES(X86)");
    if (pf86) {
        paths.push_back(std::string(pf86) + "\\VSTPlugins");
        paths.push_back(std::string(pf86) + "\\Steinberg\\VSTPlugins");
    }
#else
    const char *home = getenv("HOME");
    if (home) paths.push_back(std::string(home) + "/.vst");
    paths.push_back("/usr/lib/vst");
    paths.push_back("/usr/local/lib/vst");
#endif

    return paths;
}

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
    std::unordered_set<int32_t> seen_ids;
    std::unordered_set<int32_t> colliding_ids;
    for (const auto &p : plugins) {
        if (!seen_ids.insert(p.unique_id).second) colliding_ids.insert(p.unique_id);
    }

    s_entries.clear();
    s_entries.reserve(plugins.size());

    for (auto &p : plugins) {
        PluginEntry entry;
        bool needs_suffix = colliding_ids.count(p.unique_id) > 0;
        entry.id = make_plugin_id_disambiguated(
            p.format, p.unique_id, p.file_path, needs_suffix);
        entry.name = p.name;
        entry.vendor = p.vendor;
        entry.version_str = format_version(p.vendor_version);
        entry.plugin_path = p.file_path;
        entry.num_inputs = p.num_inputs;
        entry.num_outputs = p.num_outputs;
        entry.num_params = p.num_params;
        entry.has_editor = (p.flags & effFlagsHasEditor) != 0;
        entry.format = p.format;
        entry.needs_x86_64_bridge = p.needs_cross_arch;

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
