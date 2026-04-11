#pragma once
//
// Keepsake configuration and scan cache.
//

#include "vst2_loader.h"
#include <string>
#include <vector>

// --- Configuration ---

struct IsolationOverride {
    std::string match;
    std::string mode; // "shared", "per-binary", "per-instance"
};

// Exposure mode: which plugins does Keepsake make visible to the host?
//   "auto"      — only plugins the host can't load natively (arch mismatch)
//   "whitelist" — only explicitly listed plugins
//   "all"       — everything found (development/testing only)
struct WhitelistEntry {
    std::string path; // exact path or glob
};

struct KeepsakeConfig {
    std::vector<std::string> extra_vst2_paths;
    bool force_rescan = false;
    std::string isolation_default = "per-instance";
    std::vector<IsolationOverride> isolation_overrides;
    std::string expose_mode = "auto"; // "auto", "whitelist", "all"
    std::vector<WhitelistEntry> whitelist;
};

// Load config.toml from the platform config directory.
// Returns default config if file doesn't exist.
KeepsakeConfig config_load();

// Get the platform config directory path.
std::string config_dir();

// --- Scan cache ---

// Load cached plugin info from cache.json.
// Returns empty vector if cache doesn't exist or is corrupt.
std::vector<Vst2PluginInfo> cache_load();

// Save plugin info to cache.json.
void cache_save(const std::vector<Vst2PluginInfo> &plugins);

// Check if a rescan sentinel file exists and remove it.
bool cache_check_rescan_sentinel();

// Get the modification time of a file (seconds since epoch).
// Returns 0 if the file doesn't exist.
int64_t file_mtime(const std::string &path);
