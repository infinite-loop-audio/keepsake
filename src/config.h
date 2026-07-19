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
//   "auto"      — apply the explicit expose.* booleans below
//   "whitelist" — only explicitly listed plugins
//   "all"       — everything found (development/testing only)
struct WhitelistEntry {
    std::string path; // exact path or glob
};

struct KeepsakeConfig {
    bool replace_default_vst2_paths = false;
    std::vector<std::string> extra_vst2_paths;
    bool force_rescan = false;
    std::string mac_ui_mode = "live"; // "live"/"floating", "auto"; "preview"/"iosurface" are diagnostic-only
    std::string mac_embed_attach_target = "auto"; // "auto", "requested-parent", "content-view", "frame-superview"
    std::string isolation_default = "per-instance";
    std::vector<IsolationOverride> isolation_overrides;
    std::string expose_mode = "auto"; // "auto", "whitelist", "all"
    bool expose_vst2_bridged = true;
    bool expose_vst2_native = false;
    bool expose_vst3_bridged = true;
    bool expose_vst3_native = false;
    bool expose_au = false;
    std::vector<WhitelistEntry> whitelist;
};

// Load config.toml from the platform config directory.
// Returns default config if file doesn't exist.
KeepsakeConfig config_load();

// Get the platform config directory path.
std::string config_dir();

// --- Scan cache ---

// Load cached plugin info from cache.dat.
// Returns an empty vector if the cache does not exist or cannot be reused.
// When provided, invalidated is set when an existing stale or corrupt cache
// requires a complete scan rather than the normal fast startup scan.
std::vector<Vst2PluginInfo> cache_load(bool *invalidated = nullptr);

// Save plugin info to cache.dat.
void cache_save(const std::vector<Vst2PluginInfo> &plugins);

// Check if a rescan sentinel file exists and remove it.
bool cache_check_rescan_sentinel();

// Get the modification time of a file (seconds since epoch).
// Returns 0 if the file doesn't exist.
int64_t file_mtime(const std::string &path);
