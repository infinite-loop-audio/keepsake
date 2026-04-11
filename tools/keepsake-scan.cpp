// keepsake-scan: standalone scanner that populates the cache.
// Run this OUTSIDE the DAW to discover all plugins (including slow VST3
// and cross-architecture VST2). The cache is then loaded instantly by
// the CLAP plugin on next host startup.
//
// Usage: keepsake-scan [--bridge <path>] [--bridge-x86_64 <path>]

#include "ipc.h"
#include "vst2_loader.h"
#include "config.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

static std::string g_bridge;
static std::string g_bridge_x86;

static bool is_vst2(const fs::path &p) {
#ifdef __APPLE__
    return p.extension() == ".vst" && fs::is_directory(p);
#elif defined(_WIN32)
    return p.extension() == ".dll" && fs::is_regular_file(p);
#else
    return p.extension() == ".so" && fs::is_regular_file(p);
#endif
}

static bool is_vst3(const fs::path &p) {
    return p.extension() == ".vst3";
}

int main(int argc, char *argv[]) {
    // Find bridge binaries
#ifdef __APPLE__
    // Default: look relative to this binary or in known locations
    g_bridge = "/Users/betterthanclay/Library/Audio/Plug-Ins/CLAP/keepsake.clap/Contents/Helpers/keepsake-bridge";
    g_bridge_x86 = "/Users/betterthanclay/Library/Audio/Plug-Ins/CLAP/keepsake.clap/Contents/Helpers/keepsake-bridge-x86_64";
#endif

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bridge") == 0 && i + 1 < argc)
            g_bridge = argv[++i];
        else if (strcmp(argv[i], "--bridge-x86_64") == 0 && i + 1 < argc)
            g_bridge_x86 = argv[++i];
    }

    printf("Keepsake Scanner\n");
    printf("Bridge: %s\n", g_bridge.c_str());
    if (!g_bridge_x86.empty())
        printf("Bridge x86_64: %s\n", g_bridge_x86.c_str());
    printf("\n");

    std::vector<Vst2PluginInfo> all_plugins;

    // --- VST2 scan ---
    std::vector<std::string> vst2_paths;
#ifdef __APPLE__
    const char *home = getenv("HOME");
    if (home) vst2_paths.push_back(std::string(home) + "/Library/Audio/Plug-Ins/VST");
    vst2_paths.push_back("/Library/Audio/Plug-Ins/VST");
#elif defined(_WIN32)
    const char *pf = getenv("PROGRAMFILES");
    if (pf) { vst2_paths.push_back(std::string(pf) + "\\VSTPlugins"); }
#else
    const char *home = getenv("HOME");
    if (home) vst2_paths.push_back(std::string(home) + "/.vst");
    vst2_paths.push_back("/usr/lib/vst");
#endif

    printf("Scanning VST2...\n");
    for (const auto &dir : vst2_paths) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto &entry : fs::directory_iterator(dir, ec)) {
            if (!is_vst2(entry.path())) continue;
            std::string path = entry.path().string();

            Vst2PluginInfo info;
            info.format = FORMAT_VST2;

            // Try native first
            if (vst2_load_metadata(path, info)) {
                all_plugins.push_back(std::move(info));
            } else if (!g_bridge_x86.empty()) {
                // Try cross-arch via Rosetta bridge
                Vst2PluginInfo cross;
                cross.format = FORMAT_VST2;
                if (scan_plugin_via_bridge(path, g_bridge_x86, FORMAT_VST2, cross)) {
                    cross.needs_cross_arch = true;
                    all_plugins.push_back(std::move(cross));
                }
            }
        }
    }
    printf("Found %zu VST2 plugin(s)\n\n", all_plugins.size());

    // --- VST3 scan ---
    std::vector<std::string> vst3_paths;
#ifdef __APPLE__
    if (home) vst3_paths.push_back(std::string(home) + "/Library/Audio/Plug-Ins/VST3");
    vst3_paths.push_back("/Library/Audio/Plug-Ins/VST3");
#elif defined(_WIN32)
    const char *cpf = getenv("COMMONPROGRAMFILES");
    if (cpf) vst3_paths.push_back(std::string(cpf) + "\\VST3");
#else
    if (home) vst3_paths.push_back(std::string(home) + "/.vst3");
    vst3_paths.push_back("/usr/lib/vst3");
#endif

    size_t vst3_start = all_plugins.size();
    printf("Scanning VST3 (this may take a while)...\n");
    for (const auto &dir : vst3_paths) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto &entry : fs::directory_iterator(dir, ec)) {
            if (!is_vst3(entry.path())) continue;
            Vst2PluginInfo info;
            if (scan_plugin_via_bridge(entry.path().string(), g_bridge,
                                        FORMAT_VST3, info)) {
                all_plugins.push_back(std::move(info));
            }
        }
    }
    printf("Found %zu VST3 plugin(s)\n\n",
           all_plugins.size() - vst3_start);

    // --- AU scan (macOS only, in-process) ---
#ifdef __APPLE__
    // AU enumeration would go here but requires AudioToolbox linked
    // into this binary. Skip for now — the CLAP plugin handles AU.
#endif

    // --- Save cache ---
    printf("Total: %zu plugins\n", all_plugins.size());
    cache_save(all_plugins);
    printf("Cache saved. Restart your DAW to see all plugins.\n");

    return 0;
}
