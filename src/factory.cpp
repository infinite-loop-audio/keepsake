#include "factory_internal.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <unordered_set>

namespace {

std::string normalize_path_key(const std::string &path) {
    if (path.empty()) return {};
    std::error_code ec;
    fs::path p(path);
    fs::path normalized = fs::exists(p, ec) ? fs::weakly_canonical(p, ec)
                                            : p.lexically_normal();
    std::string key = normalized.empty() ? p.lexically_normal().string()
                                         : normalized.string();
#ifdef _WIN32
    std::replace(key.begin(), key.end(), '/', '\\');
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return key;
}

void append_unique_scan_path(std::vector<std::string> &paths,
                             std::unordered_set<std::string> &seen,
                             const std::string &path) {
    if (path.empty()) return;
    std::string key = normalize_path_key(path);
    if (key.empty()) key = path;
    if (!seen.insert(key).second) return;
    paths.push_back(path);
}

void dedupe_plugins_by_file_path(std::vector<Vst2PluginInfo> &plugins) {
    std::unordered_set<std::string> seen;
    std::vector<Vst2PluginInfo> deduped;
    deduped.reserve(plugins.size());

    for (auto &plugin : plugins) {
        std::string key = normalize_path_key(plugin.file_path);
        if (key.empty()) key = plugin.file_path;
        if (!seen.insert(key).second) {
            fprintf(stderr, "keepsake: deduped duplicate scan result '%s'\n",
                    plugin.file_path.c_str());
            continue;
        }
        deduped.push_back(std::move(plugin));
    }

    plugins = std::move(deduped);
}

} // namespace

std::vector<PluginEntry> s_entries;
std::string s_bridge_path;
std::string s_bridge_x86_64_path;
std::string s_bridge_32_path;
BridgePool s_pool;
KeepsakeConfig s_config;

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
                e.num_inputs, e.num_outputs, e.num_params, e.has_editor, e.format,
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
    bool targeted_vst2_override = (env && env[0] != '\0');
    if (targeted_vst2_override) {
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
    auto vst2_paths = cfg.replace_default_vst2_paths
                    ? std::vector<std::string>{}
                    : get_scan_paths();
    std::unordered_set<std::string> seen_scan_paths;
    std::vector<std::string> deduped_vst2_paths;
    deduped_vst2_paths.reserve(vst2_paths.size() + cfg.extra_vst2_paths.size());
    for (const auto &path : vst2_paths) {
        append_unique_scan_path(deduped_vst2_paths, seen_scan_paths, path);
    }
    for (const auto &path : cfg.extra_vst2_paths) {
        append_unique_scan_path(deduped_vst2_paths, seen_scan_paths, path);
    }
    vst2_paths = std::move(deduped_vst2_paths);

    bool allow_cross_arch_vst2_scan =
        targeted_vst2_override || cfg.replace_default_vst2_paths;

    fprintf(stderr, "keepsake: scanning %zu VST2 path(s)\n", vst2_paths.size());
    for (const auto &path : vst2_paths) {
        scan_vst2_entry(path, all_plugins, allow_cross_arch_vst2_scan);
        // Cross-arch plugins require keepsake-scan to populate the
        // cache. They're too slow to scan during host init.
    }
    dedupe_plugins_by_file_path(all_plugins);
    fprintf(stderr, "keepsake: found %zu VST2 plugin(s)\n", all_plugins.size());

    // VST3 and AU scanning is deferred — too slow for host init.
    // These formats are scanned via bridge subprocesses which spawn
    // one process per plugin. Run a manual rescan (rescan sentinel
    // or config.toml rescan=true) to discover VST3 and AU plugins.
    // Once scanned, they're cached and load instantly on subsequent runs.
    //
    // Future improvement: move VST3/AU discovery off the host-init path while
    // keeping descriptor state deterministic across hosts.
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
            dedupe_plugins_by_file_path(all_plugins);
            fprintf(stderr, "keepsake: found %zu VST3 plugin(s)\n",
                    all_plugins.size() - before);
        }

        // Full scan — AU v2 (macOS only)
#ifdef __APPLE__
        {
            size_t before = all_plugins.size();
            scan_au_plugins(all_plugins);
            dedupe_plugins_by_file_path(all_plugins);
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

    // Targeted env scans are an explicit request for those exact plugins.
    // Do not hide them behind the default auto-expose policy.
    if (!targeted_vst2_override) {
        filter_plugins(all_plugins, cfg);
    }

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
