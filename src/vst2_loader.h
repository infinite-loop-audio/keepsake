#pragma once

#include <string>
#include <cstdint>

// Metadata extracted from a VST2 plugin binary via VeSTige.
// This struct is populated by vst2_load_metadata() and consumed
// by the factory to build CLAP descriptors.
struct Vst2PluginInfo {
    int32_t unique_id;
    int32_t vendor_version;
    int32_t category;       // VstPlugCategory value
    int32_t flags;          // AEffect flags (effFlagsIsSynth, etc.)
    int32_t num_inputs;
    int32_t num_outputs;
    int32_t num_params;
    int32_t num_programs;
    std::string name;
    std::string vendor;
    std::string product;
    std::string file_path;  // absolute path to the plugin binary
    bool needs_cross_arch = false; // needs a different-architecture bridge
    uint32_t format = 0; // PluginFormat enum (0=VST2, 1=VST3, 2=AU)
};

// Load a VST2 plugin binary, extract its metadata, then close it.
// The plugin is opened only long enough to read its AEffect fields
// and dispatch metadata queries — no audio processing occurs.
//
// Returns true on success. On failure, logs to stderr and returns false.
bool vst2_load_metadata(const std::string &path, Vst2PluginInfo &info);

// Extract metadata from a VST2 plugin via a bridge subprocess.
// Used for cross-architecture plugins (e.g., x86_64 on ARM).
// The bridge binary must be built for the plugin's architecture.
//
// Returns true on success.
bool vst2_load_metadata_via_bridge(const std::string &path,
                                    const std::string &bridge_binary,
                                    Vst2PluginInfo &info);

// Extract metadata from any plugin via a bridge subprocess.
// Format-aware: sends the correct format ID to the bridge.
// Used for crash-safe scanning (all formats) and cross-architecture.
//
// If bridge_proc is provided (non-null), reuses an existing bridge process
// (multi-instance mode). Otherwise spawns a temporary one.
//
// Returns true on success. On bridge crash, returns false.
bool scan_plugin_via_bridge(const std::string &path,
                             const std::string &bridge_binary,
                             uint32_t format,
                             Vst2PluginInfo &info);
