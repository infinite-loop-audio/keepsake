#pragma once

#include "factory.h"
#include "plugin.h"
#include "vst2_loader.h"
#include "config.h"
#include "bridge_pool.h"
#include <vestige/vestige.h>
#include <clap/clap.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct PluginEntry {
    std::string id;
    std::string name;
    std::string vendor;
    std::string version_str;
    std::vector<std::string> feature_strings;
    std::vector<const char *> features_ptrs;
    clap_plugin_descriptor_t descriptor;
    std::string plugin_path;
    int32_t num_inputs = 0;
    int32_t num_outputs = 0;
    int32_t num_params = 0;
    bool has_editor = false;
    uint32_t format = 0;
    std::string binary_arch;
    bool needs_x86_64_bridge = false;
    bool needs_32bit_bridge = false;
};

extern std::vector<PluginEntry> s_entries;
extern std::string s_bridge_path;
extern std::string s_bridge_x86_64_path;
extern std::string s_bridge_32_path;
extern BridgePool s_pool;
extern KeepsakeConfig s_config;

std::string format_version(int32_t v);
void scan_vst2_entry(const std::string &entry_path,
                     std::vector<Vst2PluginInfo> &results,
                     bool targeted_vst2_override);
void scan_vst3_directory(const std::string &dir_path,
                         std::vector<Vst2PluginInfo> &results);
void scan_au_plugins(std::vector<Vst2PluginInfo> &results);
std::vector<std::string> get_scan_paths();
void filter_plugins(std::vector<Vst2PluginInfo> &plugins,
                    const KeepsakeConfig &cfg);
void build_descriptors(std::vector<Vst2PluginInfo> &plugins);
