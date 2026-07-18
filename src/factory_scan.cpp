#include "factory_internal.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>

namespace {

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

template <typename Fn>
void walk_vst2_plugins(const fs::path &root, Fn &&fn) {
    std::error_code ec;
    if (is_vst2_file(root)) {
        fn(root);
        return;
    }
    if (!fs::is_directory(root, ec)) return;

    fs::recursive_directory_iterator it(
        root,
        fs::directory_options::follow_directory_symlink |
            fs::directory_options::skip_permission_denied,
        ec);
    fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const fs::path &path = it->path();
        if (!is_vst2_file(path)) continue;
        fn(path);
        it.disable_recursion_pending();
    }
}

} // namespace

static void emit_scan_progress(const char *state,
                               const char *format,
                               bool bridged,
                               const fs::path &path) {
    fprintf(stderr, "keepsake: scan-progress state=%s format=%s bridged=%d path=%s\n",
            state, format, bridged ? 1 : 0, path.string().c_str());
}

static std::optional<Vst2PluginInfo> scan_vst2_plugin(
    const fs::path &plugin_path,
    bool targeted_vst2_override,
    const KeepsakeConfig &cfg) {
    std::string binary_arch = vst2_detect_binary_arch(plugin_path.string());
    bool needs_cross_arch = !binary_arch.empty() && binary_arch != "native";
    emit_scan_progress("started", "vst2", needs_cross_arch, plugin_path);
    Vst2PluginInfo info;
    info.format = FORMAT_VST2;
    if (vst2_load_metadata(plugin_path.string(), info)) {
        if (targeted_vst2_override || plugin_is_exposed(info, cfg)) {
            emit_scan_progress("discovered", "vst2", info.needs_cross_arch, plugin_path);
        }
        return info;
    }
    std::string bridge_binary;
#ifdef _WIN32
    if (binary_arch == "x86" && !s_bridge_32_path.empty()) {
        bridge_binary = s_bridge_32_path;
    }
#endif
#ifdef __APPLE__
    if (bridge_binary.empty() &&
        binary_arch == "x86_64" && !s_bridge_x86_64_path.empty()) {
        bridge_binary = s_bridge_x86_64_path;
    }
#endif
    if (bridge_binary.empty() &&
        targeted_vst2_override && !s_bridge_x86_64_path.empty()) {
        bridge_binary = s_bridge_x86_64_path;
    }
    if (!bridge_binary.empty()) {
        Vst2PluginInfo cross_info;
        cross_info.format = FORMAT_VST2;
        if (vst2_load_metadata_via_bridge(plugin_path.string(),
                                          bridge_binary,
                                          cross_info)) {
            if (cross_info.binary_arch.empty()) cross_info.binary_arch = binary_arch;
            if (targeted_vst2_override || plugin_is_exposed(cross_info, cfg)) {
                emit_scan_progress("discovered", "vst2", true, plugin_path);
            }
            return cross_info;
        }
    }
    emit_scan_progress("failed", "vst2", needs_cross_arch, plugin_path);
    return std::nullopt;
}

void scan_vst2_entry(const std::string &entry_path,
                     std::vector<Vst2PluginInfo> &results,
                     bool targeted_vst2_override,
                     const KeepsakeConfig &cfg) {
    fs::path entry(entry_path);

    std::vector<fs::path> paths;
    walk_vst2_plugins(entry, [&](const fs::path &path) { paths.push_back(path); });
    std::atomic<size_t> next{0};
    std::mutex result_mutex;
    std::vector<Vst2PluginInfo> scanned;
    const size_t worker_count = std::min<size_t>(4, paths.size());
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&] {
            while (true) {
                const size_t index = next.fetch_add(1);
                if (index >= paths.size()) break;
                auto plugin = scan_vst2_plugin(paths[index], targeted_vst2_override, cfg);
                if (!plugin) continue;
                std::lock_guard<std::mutex> lock(result_mutex);
                scanned.push_back(std::move(*plugin));
            }
        });
    }
    for (auto &worker : workers) worker.join();
    std::sort(scanned.begin(), scanned.end(), [](const auto &left, const auto &right) {
        return left.file_path < right.file_path;
    });
    results.insert(results.end(),
                   std::make_move_iterator(scanned.begin()),
                   std::make_move_iterator(scanned.end()));
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
            CFStringGetCString(cf_name, name_buf, sizeof(name_buf), kCFStringEncodingUTF8);
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
                         std::vector<Vst2PluginInfo> &results,
                         bool scan_native,
                         bool scan_bridged,
                         const KeepsakeConfig &cfg) {
    std::error_code ec;
    if (!fs::is_directory(dir_path, ec)) return;

    fs::recursive_directory_iterator it(
        dir_path,
        fs::directory_options::follow_directory_symlink |
            fs::directory_options::skip_permission_denied,
        ec);
    fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const fs::path &path = it->path();
        if (!is_vst3_file(path)) continue;
        it.disable_recursion_pending();

        std::string binary_arch = vst2_detect_binary_arch(path.string());
        bool needs_cross_arch = !binary_arch.empty() && binary_arch != "native";
        if ((needs_cross_arch && !scan_bridged) ||
            (!needs_cross_arch && !scan_native)) {
            continue;
        }

        emit_scan_progress("started", "vst3", needs_cross_arch, path);

        std::string bridge_binary = s_bridge_path;
#ifdef __APPLE__
        if (needs_cross_arch && binary_arch == "x86_64" &&
            !s_bridge_x86_64_path.empty()) {
            bridge_binary = s_bridge_x86_64_path;
        }
#endif
        if (bridge_binary.empty()) continue;

        Vst2PluginInfo info;
        if (scan_plugin_via_bridge(path.string(),
                                   bridge_binary, FORMAT_VST3, info)) {
            info.binary_arch = binary_arch;
            info.needs_cross_arch = needs_cross_arch;
            if (plugin_is_exposed(info, cfg)) {
                emit_scan_progress("discovered", "vst3", needs_cross_arch, path);
            }
            results.push_back(std::move(info));
        } else {
            emit_scan_progress("failed", "vst3", needs_cross_arch, path);
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
    auto append_windows_paths = [&](const char *base) {
        if (!base || base[0] == '\0') return;
        paths.push_back(std::string(base) + "\\VST2");
        paths.push_back(std::string(base) + "\\VSTPlugins");
        paths.push_back(std::string(base) + "\\Steinberg\\VSTPlugins");
    };

    const char *pf = getenv("PROGRAMFILES");
    append_windows_paths(pf);
    const char *pf86 = getenv("PROGRAMFILES(X86)");
    append_windows_paths(pf86);
    const char *common = getenv("COMMONPROGRAMFILES");
    append_windows_paths(common);
    const char *common86 = getenv("COMMONPROGRAMFILES(X86)");
    append_windows_paths(common86);
#else
    const char *home = getenv("HOME");
    if (home) paths.push_back(std::string(home) + "/.vst");
    paths.push_back("/usr/lib/vst");
    paths.push_back("/usr/local/lib/vst");
#endif

    return paths;
}
