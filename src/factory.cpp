#include "factory.h"
#include "plugin.h"
#include "vst2_loader.h"
#include "config.h"
#include "bridge_pool.h"
#include <vestige/vestige.h>
#include <clap/clap.h>

#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unordered_set>
#include <filesystem>

namespace fs = std::filesystem;

// --- Descriptor storage ---
// Each PluginEntry owns the strings that the clap_plugin_descriptor_t
// points to. Entries live in a stable vector from init() to deinit().

struct PluginEntry {
    std::string id;
    std::string name;
    std::string vendor;
    std::string version_str;
    std::vector<std::string> feature_strings;
    std::vector<const char *> features_ptrs; // null-terminated for CLAP
    clap_plugin_descriptor_t descriptor;
    std::string plugin_path;
    int32_t num_inputs = 0;
    int32_t num_outputs = 0;
    uint32_t format = 0; // PluginFormat
    bool needs_x86_64_bridge = false;
    bool needs_32bit_bridge = false;
};

static std::vector<PluginEntry> s_entries;
static std::string s_bridge_path;
static std::string s_bridge_x86_64_path;
static std::string s_bridge_32_path;
static BridgePool s_pool;

// --- Version formatting ---
// VST2 has no standard version encoding. Two common conventions:
//   Hex-nibble: 0xMMmmpp → M.m.p  (e.g., 0x010203 = 66051 → 1.2.3)
//   Decimal:    MMMmmpp  → M.m.p  (e.g., 1310 → 1.3.10)
// We try hex first (most common in practice), then decimal, then raw.

static std::string format_version(int32_t v) {
    if (v <= 0) return "0.0.0";

    char buf[32];

    // Try hex-nibble decomposition: each byte is a version component
    int hex_major = (v >> 16) & 0xFF;
    int hex_minor = (v >> 8) & 0xFF;
    int hex_patch = v & 0xFF;

    if (hex_major >= 1 && hex_major < 100 &&
        hex_minor < 100 && hex_patch < 100 && v > 0xFFFF) {
        snprintf(buf, sizeof(buf), "%d.%d.%d", hex_major, hex_minor, hex_patch);
        return buf;
    }

    // Try decimal decomposition: MAJOR * 1000 + MINOR * 100 + PATCH
    if (v >= 1000) {
        int dec_major = v / 1000;
        int dec_minor = (v / 100) % 10;
        int dec_patch = v % 100;
        snprintf(buf, sizeof(buf), "%d.%d.%d", dec_major, dec_minor, dec_patch);
        return buf;
    }

    // Small value — display as-is
    snprintf(buf, sizeof(buf), "%d", v);
    return buf;
}

// --- Plugin ID generation ---

static const char *format_prefix(uint32_t format) {
    switch (format) {
    case FORMAT_VST2: return "vst2";
    case FORMAT_VST3: return "vst3";
    case FORMAT_AU:   return "au";
    default:          return "unknown";
    }
}

static std::string make_plugin_id(uint32_t format, int32_t unique_id) {
    char buf[64];
    snprintf(buf, sizeof(buf), "keepsake.%s.%08X",
             format_prefix(format), static_cast<uint32_t>(unique_id));
    return buf;
}

// Collision-aware ID generation: if the same uniqueID appears more than
// once, disambiguate with a hash suffix derived from the file path.
static std::string make_plugin_id_disambiguated(
    uint32_t format, int32_t unique_id,
    const std::string &path, bool needs_suffix)
{
    std::string base = make_plugin_id(format, unique_id);
    if (!needs_suffix) return base;

    // Simple hash of the file path for disambiguation
    uint32_t hash = 0;
    for (char c : path) {
        hash = hash * 31 + static_cast<uint32_t>(c);
    }
    char suffix[8];
    snprintf(suffix, sizeof(suffix), ".%04x", hash & 0xFFFF);
    return base + suffix;
}

// --- Feature mapping (contract 002) ---

static void map_features(const Vst2PluginInfo &info, PluginEntry &entry) {
    bool is_synth = (info.flags & effFlagsIsSynth) != 0;

    if (is_synth) {
        entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_INSTRUMENT);
    }

    switch (info.category) {
    case kPlugCategSynth:
        if (!is_synth)
            entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_INSTRUMENT);
        entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_SYNTHESIZER);
        break;
    case kPlugCategEffect:
        if (!is_synth)
            entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_AUDIO_EFFECT);
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
        if (!is_synth)
            entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_AUDIO_EFFECT);
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
        if (!is_synth)
            entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_INSTRUMENT);
        break;
    case kPlugCategUnknown:
    default:
        if (!is_synth)
            entry.feature_strings.push_back(CLAP_PLUGIN_FEATURE_AUDIO_EFFECT);
        break;
    }

    // Build the null-terminated features pointer array
    entry.features_ptrs.clear();
    for (const auto &f : entry.feature_strings) {
        entry.features_ptrs.push_back(f.c_str());
    }
    entry.features_ptrs.push_back(nullptr);
}

// --- Directory scanning ---

static bool is_vst2_file(const fs::path &p) {
#ifdef __APPLE__
    // macOS: .vst bundles (directories)
    return p.extension() == ".vst" && fs::is_directory(p);
#elif defined(_WIN32)
    return p.extension() == ".dll" && fs::is_regular_file(p);
#else
    return p.extension() == ".so" && fs::is_regular_file(p);
#endif
}

static void scan_vst2_directory(const std::string &dir_path,
                                std::vector<Vst2PluginInfo> &results) {
    std::error_code ec;
    if (!fs::is_directory(dir_path, ec)) return;

    for (const auto &dir_entry : fs::directory_iterator(dir_path, ec)) {
        if (ec) break;
        if (!is_vst2_file(dir_entry.path())) continue;

        std::string plugin_path = dir_entry.path().string();

        // Scan via bridge subprocess (crash-safe)
        Vst2PluginInfo info;
        if (scan_plugin_via_bridge(plugin_path, s_bridge_path,
                                    FORMAT_VST2, info)) {
            results.push_back(std::move(info));
        } else if (!s_bridge_x86_64_path.empty()) {
            // Try cross-architecture bridge
            Vst2PluginInfo cross_info;
            if (scan_plugin_via_bridge(plugin_path, s_bridge_x86_64_path,
                                        FORMAT_VST2, cross_info)) {
                cross_info.needs_cross_arch = true;
                results.push_back(std::move(cross_info));
            }
        }
    }
}

// Scan AU v2 plugins via the bridge subprocess (macOS only).
// We spawn the bridge for each AU component to extract metadata.
#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>

static void scan_au_plugins(std::vector<Vst2PluginInfo> &results) {
    // Enumerate all AU components
    AudioComponentDescription desc = {};
    desc.componentType = 0; // any type
    AudioComponent comp = nullptr;

    while ((comp = AudioComponentFindNext(comp, &desc)) != nullptr) {
        AudioComponentDescription cd = {};
        AudioComponentGetDescription(comp, &cd);

        // Only instrument and effect types
        if (cd.componentType != kAudioUnitType_MusicDevice &&
            cd.componentType != kAudioUnitType_MusicEffect &&
            cd.componentType != kAudioUnitType_Effect)
            continue;

        // Build the AU path encoding: "type:subtype:manufacturer"
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

        // Get component name for metadata
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

            // Name format: "Manufacturer: Plugin Name"
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

    fprintf(stderr, "keepsake: enumerated %zu AU plugin(s)\n",
            results.size());
}
#endif

static bool is_vst3_file(const fs::path &p) {
    return p.extension() == ".vst3";
}

static void scan_vst3_directory(const std::string &dir_path,
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

static std::vector<std::string> get_scan_paths() {
    std::vector<std::string> paths;

    // Environment variable override
    const char *env = getenv("KEEPSAKE_VST2_PATH");
    if (env && env[0] != '\0') {
        // Split on ':' (Unix) or ';' (Windows)
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
            if (end > start) {
                paths.push_back(s.substr(start, end - start));
            }
            start = end + 1;
        }
        return paths;
    }

    // Platform defaults
#ifdef __APPLE__
    // macOS standard VST2 paths
    const char *home = getenv("HOME");
    if (home) {
        paths.push_back(std::string(home) + "/Library/Audio/Plug-Ins/VST");
    }
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
    // Linux
    const char *home = getenv("HOME");
    if (home) {
        paths.push_back(std::string(home) + "/.vst");
    }
    paths.push_back("/usr/lib/vst");
    paths.push_back("/usr/local/lib/vst");
#endif

    return paths;
}

// --- Exposure filtering ---
// Filter plugins based on the expose mode before building descriptors.

static KeepsakeConfig s_config; // stored for filtering

static bool glob_match_simple(const std::string &pattern, const std::string &text) {
    if (pattern == text) return true;
    if (pattern == "*") return true;
    // Simple suffix match: "*.vst" matches "/path/to/foo.vst"
    if (pattern.size() > 1 && pattern[0] == '*') {
        std::string suffix = pattern.substr(1);
        return text.size() >= suffix.size() &&
               text.substr(text.size() - suffix.size()) == suffix;
    }
    // Simple contains: "*foo*" matches "/path/to/foobar.vst"
    if (pattern.size() > 2 && pattern[0] == '*' && pattern.back() == '*') {
        std::string needle = pattern.substr(1, pattern.size() - 2);
        return text.find(needle) != std::string::npos;
    }
    return false;
}

static void filter_plugins(std::vector<Vst2PluginInfo> &plugins,
                             const KeepsakeConfig &cfg) {
    if (cfg.expose_mode == "all") return; // no filtering

    std::vector<Vst2PluginInfo> filtered;

    for (auto &p : plugins) {
        bool include = false;

        if (cfg.expose_mode == "auto") {
            // Only expose plugins that need cross-architecture bridging
            // — i.e., plugins the host can't load natively
            include = p.needs_cross_arch;
        } else if (cfg.expose_mode == "whitelist") {
            // Only expose explicitly listed plugins
            for (const auto &wl : cfg.whitelist) {
                if (glob_match_simple(wl.path, p.file_path)) {
                    include = true;
                    break;
                }
            }
        }

        if (include) {
            filtered.push_back(std::move(p));
        }
    }

    size_t removed = plugins.size() - filtered.size();
    plugins = std::move(filtered);
    if (removed > 0) {
        fprintf(stderr, "keepsake: filtered to %zu plugins (removed %zu, mode=%s)\n",
                plugins.size(), removed, cfg.expose_mode.c_str());
    }
}

// --- Build descriptors from scan results ---

static void build_descriptors(std::vector<Vst2PluginInfo> &plugins) {
    // Detect uniqueID collisions
    std::unordered_set<int32_t> seen_ids;
    std::unordered_set<int32_t> colliding_ids;
    for (const auto &p : plugins) {
        if (!seen_ids.insert(p.unique_id).second) {
            colliding_ids.insert(p.unique_id);
        }
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
        entry.format = p.format;
        entry.needs_x86_64_bridge = p.needs_cross_arch;

        map_features(p, entry);

        // Build the CLAP descriptor — points into this entry's owned strings
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

    // After moving into s_entries, re-point descriptor fields to the
    // stable strings (std::string move preserves content but the c_str()
    // pointers are now in s_entries, not the moved-from objects).
    for (auto &e : s_entries) {
        e.descriptor.id = e.id.c_str();
        e.descriptor.name = e.name.c_str();
        e.descriptor.vendor = e.vendor.c_str();
        e.descriptor.version = e.version_str.c_str();

        // Rebuild features_ptrs to point to stable strings
        e.features_ptrs.clear();
        for (const auto &f : e.feature_strings) {
            e.features_ptrs.push_back(f.c_str());
        }
        e.features_ptrs.push_back(nullptr);
        e.descriptor.features = e.features_ptrs.data();
    }
}

// --- CLAP factory callbacks ---

static uint32_t factory_get_plugin_count(
    const clap_plugin_factory_t * /*factory*/)
{
    return static_cast<uint32_t>(s_entries.size());
}

static const clap_plugin_descriptor_t *factory_get_plugin_descriptor(
    const clap_plugin_factory_t * /*factory*/, uint32_t index)
{
    if (index >= s_entries.size()) return nullptr;
    return &s_entries[index].descriptor;
}

static const clap_plugin_t *factory_create_plugin(
    const clap_plugin_factory_t * /*factory*/,
    const clap_host_t *host,
    const char *plugin_id)
{
    if (!plugin_id || !host) return nullptr;

    for (auto &e : s_entries) {
        if (e.id == plugin_id) {
            const std::string *bridge = &s_bridge_path;
            if (e.needs_32bit_bridge && !s_bridge_32_path.empty())
                bridge = &s_bridge_32_path;
            else if (e.needs_x86_64_bridge && !s_bridge_x86_64_path.empty())
                bridge = &s_bridge_x86_64_path;

            auto isolation = s_pool.resolve_mode(e.id, e.name);
            return keepsake_plugin_create(
                host, &e.descriptor, e.plugin_path, *bridge,
                e.num_inputs, e.num_outputs, e.format,
                &s_pool, isolation);
        }
    }
    return nullptr;
}

static const clap_plugin_factory_t s_factory = {
    .get_plugin_count = factory_get_plugin_count,
    .get_plugin_descriptor = factory_get_plugin_descriptor,
    .create_plugin = factory_create_plugin,
};

// --- Public API ---

bool keepsake_factory_init(const char *plugin_path) {
    // Determine bridge binary paths
    if (plugin_path) {
#ifdef __APPLE__
        std::string helpers = std::string(plugin_path) + "/Contents/Helpers";
        s_bridge_path = helpers + "/keepsake-bridge";
        s_bridge_x86_64_path = helpers + "/keepsake-bridge-x86_64";
#elif defined(_WIN32)
        std::string dir(plugin_path);
        size_t sep = dir.find_last_of('\\');
        if (sep != std::string::npos) dir = dir.substr(0, sep);
        s_bridge_path = dir + "\\keepsake-bridge.exe";
        s_bridge_32_path = dir + "\\keepsake-bridge-32.exe";
#else
        std::string dir(plugin_path);
        size_t sep = dir.find_last_of('/');
        if (sep != std::string::npos) dir = dir.substr(0, sep);
        s_bridge_path = dir + "/keepsake-bridge";
        s_bridge_32_path = dir + "/keepsake-bridge-32";
#endif
    }
    fprintf(stderr, "keepsake: bridge at '%s'\n", s_bridge_path.c_str());
    if (!s_bridge_x86_64_path.empty()) {
        fprintf(stderr, "keepsake: x86_64 bridge at '%s'\n",
                s_bridge_x86_64_path.c_str());
    }

    // Load configuration
    KeepsakeConfig cfg;
    bool use_cache = true;
    const char *env = getenv("KEEPSAKE_VST2_PATH");
    if (env && env[0] != '\0') {
        use_cache = false; // env override bypasses cache
    } else {
        cfg = config_load();
    }

    // Configure isolation policy
    if (cfg.isolation_default == "per-binary")
        s_pool.set_default_mode(IsolationMode::PER_BINARY);
    else if (cfg.isolation_default == "per-instance")
        s_pool.set_default_mode(IsolationMode::PER_INSTANCE);
    else
        s_pool.set_default_mode(IsolationMode::SHARED);

    for (const auto &ov : cfg.isolation_overrides) {
        IsolationMode m = IsolationMode::SHARED;
        if (ov.mode == "per-binary") m = IsolationMode::PER_BINARY;
        else if (ov.mode == "per-instance") m = IsolationMode::PER_INSTANCE;
        s_pool.add_override(ov.match, m);
    }

    // Check for rescan triggers
    bool force_rescan = cfg.force_rescan || cache_check_rescan_sentinel();

    // Try loading from cache
    std::vector<Vst2PluginInfo> all_plugins;
    if (use_cache && !force_rescan) {
        all_plugins = cache_load();
        if (!all_plugins.empty()) {
            fprintf(stderr, "keepsake: using cached scan (%zu plugins)\n",
                    all_plugins.size());
            filter_plugins(all_plugins, cfg);
            build_descriptors(all_plugins);
            return true;
        }
    }

    // Full scan — VST2 (in-process for speed during host scan)
    auto vst2_paths = get_scan_paths();
    for (const auto &p : cfg.extra_vst2_paths) {
        vst2_paths.push_back(p);
    }

    fprintf(stderr, "keepsake: scanning %zu VST2 path(s)\n", vst2_paths.size());
    for (const auto &dir : vst2_paths) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto &dir_entry : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!is_vst2_file(dir_entry.path())) continue;

            Vst2PluginInfo info;
            info.format = FORMAT_VST2;
            // Fast in-process loading (native arch only)
            if (vst2_load_metadata(dir_entry.path().string(), info)) {
                all_plugins.push_back(std::move(info));
            }
            // Cross-arch plugins require keepsake-scan to populate the
            // cache. They're too slow to scan during host init.
        }
    }
    fprintf(stderr, "keepsake: found %zu VST2 plugin(s)\n", all_plugins.size());

    // VST3 and AU scanning is deferred — too slow for host init.
    // These formats are scanned via bridge subprocesses which spawn
    // one process per plugin. Run a manual rescan (rescan sentinel
    // or config.toml rescan=true) to discover VST3 and AU plugins.
    // Once scanned, they're cached and load instantly on subsequent runs.
    //
    // TODO: background thread scanning so init returns immediately
    // while VST3/AU discovery happens in parallel.
    if (force_rescan) {
        // Only do the expensive scans when explicitly requested
        // Full scan — VST3 (via bridge subprocess)
        {
            std::vector<std::string> vst3_paths;
#ifdef __APPLE__
            const char *home2 = getenv("HOME");
            if (home2) vst3_paths.push_back(std::string(home2) + "/Library/Audio/Plug-Ins/VST3");
            vst3_paths.push_back("/Library/Audio/Plug-Ins/VST3");
#elif defined(_WIN32)
            const char *cpf = getenv("COMMONPROGRAMFILES");
            if (cpf) vst3_paths.push_back(std::string(cpf) + "\\VST3");
#else
            const char *home2 = getenv("HOME");
            if (home2) vst3_paths.push_back(std::string(home2) + "/.vst3");
            vst3_paths.push_back("/usr/lib/vst3");
            vst3_paths.push_back("/usr/local/lib/vst3");
#endif
            size_t before = all_plugins.size();
            for (const auto &dir : vst3_paths) {
                scan_vst3_directory(dir, all_plugins);
            }
            fprintf(stderr, "keepsake: found %zu VST3 plugin(s)\n",
                    all_plugins.size() - before);
        }

        // Full scan — AU v2 (macOS only)
#ifdef __APPLE__
        {
            size_t before = all_plugins.size();
            scan_au_plugins(all_plugins);
            fprintf(stderr, "keepsake: found %zu AU plugin(s)\n",
                    all_plugins.size() - before);
        }
#endif
    }

    fprintf(stderr, "keepsake: total %zu plugin(s) across all formats\n",
            all_plugins.size());

    // Save cache (full scan results, before filtering)
    if (use_cache) {
        cache_save(all_plugins);
    }

    // Filter to only expose plugins the host can't load natively
    filter_plugins(all_plugins, cfg);

    build_descriptors(all_plugins);
    return true;
}

void keepsake_factory_deinit(void) {
    s_pool.shutdown_all();
    s_entries.clear();
}

const clap_plugin_factory_t *keepsake_get_plugin_factory(void) {
    return &s_factory;
}

const char *keepsake_lookup_vst2_path(const char *plugin_id) {
    if (!plugin_id) return nullptr;
    for (const auto &e : s_entries) {
        if (e.id == plugin_id) return e.plugin_path.c_str();
    }
    return nullptr;
}

bool keepsake_lookup_plugin_info(const char *plugin_id,
                                  int32_t *num_inputs,
                                  int32_t *num_outputs) {
    if (!plugin_id) return false;
    for (const auto &e : s_entries) {
        if (e.id == plugin_id) {
            if (num_inputs) *num_inputs = e.num_inputs;
            if (num_outputs) *num_outputs = e.num_outputs;
            return true;
        }
    }
    return false;
}

const char *keepsake_get_bridge_path(void) {
    return s_bridge_path.c_str();
}
