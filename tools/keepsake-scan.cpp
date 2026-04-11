// keepsake-scan: discover plugins the host can't load natively.
//
// Default: checks VST2 plugin architectures WITHOUT loading them.
// x86_64-only plugins are flagged for Keepsake bridging. Instant.
//
// --deep: also loads each x86_64 plugin via bridge to get full metadata
//         (name, vendor, params). Slower but gives better descriptions.

#include "ipc.h"
#include "vst2_loader.h"
#include "config.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

static std::string g_bridge_x86;
static bool g_deep = false;

static bool is_vst2(const fs::path &p) {
#ifdef __APPLE__
    return p.extension() == ".vst" && fs::is_directory(p);
#elif defined(_WIN32)
    return p.extension() == ".dll" && fs::is_regular_file(p);
#else
    return p.extension() == ".so" && fs::is_regular_file(p);
#endif
}

// Extract filename stem from path
static std::string stem(const std::string &path) {
    size_t sep = path.find_last_of("/\\");
    size_t start = (sep != std::string::npos) ? sep + 1 : 0;
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos && dot > start)
        return path.substr(start, dot - start);
    return path.substr(start);
}

int main(int argc, char *argv[]) {
#ifdef __APPLE__
    g_bridge_x86 = "/Users/betterthanclay/Library/Audio/Plug-Ins/CLAP/keepsake.clap/Contents/Helpers/keepsake-bridge-x86_64";
#endif

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--deep") == 0) g_deep = true;
        else if (strcmp(argv[i], "--bridge-x86_64") == 0 && i + 1 < argc)
            g_bridge_x86 = argv[++i];
    }

    printf("Keepsake Scanner%s\n\n", g_deep ? " (deep)" : "");

    std::vector<Vst2PluginInfo> results;

    // VST2 scan paths
    std::vector<std::string> vst2_paths;
#ifdef __APPLE__
    const char *home = getenv("HOME");
    if (home) vst2_paths.push_back(std::string(home) + "/Library/Audio/Plug-Ins/VST");
    vst2_paths.push_back("/Library/Audio/Plug-Ins/VST");
#elif defined(_WIN32)
    const char *pf = getenv("PROGRAMFILES");
    if (pf) vst2_paths.push_back(std::string(pf) + "\\VSTPlugins");
#else
    if (home) vst2_paths.push_back(std::string(home) + "/.vst");
    vst2_paths.push_back("/usr/lib/vst");
#endif

    int native = 0, cross = 0, fail = 0;

    for (const auto &dir : vst2_paths) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;

        for (const auto &entry : fs::directory_iterator(dir, ec)) {
            if (!is_vst2(entry.path())) continue;
            std::string path = entry.path().string();

            // Try native in-process load (fast)
            Vst2PluginInfo info;
            info.format = FORMAT_VST2;
            if (vst2_load_metadata(path, info)) {
                native++;
                continue; // Host handles this natively
            }

            // Native failed — this is a candidate for Keepsake
            cross++;

            if (g_deep && !g_bridge_x86.empty()) {
                // Deep mode: load via bridge for full metadata
                Vst2PluginInfo deep_info;
                deep_info.format = FORMAT_VST2;
                if (scan_plugin_via_bridge(path, g_bridge_x86,
                                            FORMAT_VST2, deep_info)) {
                    deep_info.needs_cross_arch = true;
                    results.push_back(std::move(deep_info));
                    printf("  [x86_64] %s — %s (%s)\n",
                           results.back().name.c_str(),
                           results.back().vendor.c_str(),
                           path.c_str());
                    continue;
                }
            }

            // Quick mode: use filename as name, minimal metadata
            Vst2PluginInfo quick;
            quick.format = FORMAT_VST2;
            quick.file_path = path;
            quick.name = stem(path);
            quick.vendor = "Unknown";
            quick.needs_cross_arch = true;
            quick.unique_id = 0; // will be assigned a hash-based ID
            // Generate a stable ID from the path
            uint32_t hash = 0;
            for (char c : path) hash = hash * 31 + static_cast<uint32_t>(c);
            quick.unique_id = static_cast<int32_t>(hash);
            results.push_back(std::move(quick));
            printf("  [x86_64] %s — %s\n", results.back().name.c_str(),
                   path.c_str());
        }
    }

    printf("\n");
    printf("Native (host handles):  %d\n", native);
    printf("Cross-arch (Keepsake):  %d\n", cross);
    printf("Keepsake will expose:   %zu plugin(s)\n", results.size());

    cache_save(results);
    printf("\nCache saved. Restart your DAW.\n");

    return 0;
}
