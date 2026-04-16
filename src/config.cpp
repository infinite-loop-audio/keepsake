//
// Keepsake configuration and scan cache.
// Uses minimal hand-rolled serialization to avoid external dependencies.
//

#include "config_internal.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

// --- Platform config directory ---

std::string config_dir() {
#ifdef __APPLE__
    const char *home = getenv("HOME");
    if (home) return std::string(home) + "/Library/Application Support/Keepsake";
#elif defined(_WIN32)
    const char *appdata = getenv("APPDATA");
    if (appdata) return std::string(appdata) + "\\Keepsake";
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg) return std::string(xdg) + "/keepsake";
    const char *home = getenv("HOME");
    if (home) return std::string(home) + "/.config/keepsake";
#endif
    return ".keepsake";
}

// --- Minimal TOML parsing (only what we need) ---

static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static bool parse_toml_bool(const std::string &value, bool fallback) {
    if (value.find("true") != std::string::npos) return true;
    if (value.find("false") != std::string::npos) return false;
    return fallback;
}

KeepsakeConfig config_load() {
    KeepsakeConfig cfg;
    std::string path = config_dir() + "/config.toml";

    std::ifstream f(path);
    if (!f.is_open()) return cfg;

    std::string line;
    bool in_scan = false;
    bool in_gui = false;
    bool in_paths_array = false;
    bool in_isolation = false;
    bool in_override = false;
    bool in_expose = false;
    bool in_whitelist_entry = false;
    IsolationOverride current_override;
    WhitelistEntry current_wl;

    while (std::getline(f, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        if (t == "[scan]") {
            in_scan = true; in_isolation = false; in_override = false;
            in_gui = false;
            continue;
        }
        if (t == "[gui]") {
            in_gui = true; in_scan = false; in_isolation = false;
            in_override = false; in_expose = false; in_whitelist_entry = false;
            continue;
        }
        if (t == "[isolation]") {
            in_isolation = true; in_scan = false; in_override = false;
            in_gui = false; in_expose = false; in_whitelist_entry = false;
            continue;
        }
        if (t == "[expose]") {
            in_expose = true; in_scan = false; in_isolation = false;
            in_gui = false; in_override = false; in_whitelist_entry = false;
            continue;
        }
        if (t == "[[expose.plugin]]") {
            if (in_whitelist_entry && !current_wl.path.empty())
                cfg.whitelist.push_back(current_wl);
            current_wl = {};
            in_whitelist_entry = true; in_expose = true;
            in_scan = false; in_gui = false; in_isolation = false; in_override = false;
            continue;
        }
        if (t == "[[isolation.override]]") {
            if (in_override && !current_override.match.empty()) {
                cfg.isolation_overrides.push_back(current_override);
            }
            current_override = {};
            in_override = true; in_scan = false; in_isolation = true;
            continue;
        }
        if (t[0] == '[' && t.find("isolation") == std::string::npos) {
            if (in_override && !current_override.match.empty()) {
                cfg.isolation_overrides.push_back(current_override);
                current_override = {};
            }
            in_scan = false; in_paths_array = false;
            in_gui = false; in_isolation = false; in_override = false;
            continue;
        }

        if (in_scan) {
            if (t.find("replace_default_vst2_paths") == 0) {
                cfg.replace_default_vst2_paths =
                    parse_toml_bool(t, cfg.replace_default_vst2_paths);
            }
            // rescan = true/false
            if (t.find("rescan") == 0) {
                cfg.force_rescan = parse_toml_bool(t, cfg.force_rescan);
            }
            // vst2_paths = [
            if (t.find("vst2_paths") == 0) {
                in_paths_array = (t.find('[') != std::string::npos);
                // Inline single-line array?
                size_t open = t.find('[');
                size_t close = t.find(']');
                if (open != std::string::npos && close != std::string::npos) {
                    // Parse inline: ["path1", "path2"]
                    std::string inner = t.substr(open + 1, close - open - 1);
                    std::istringstream ss(inner);
                    std::string token;
                    while (std::getline(ss, token, ',')) {
                        token = trim(token);
                        if (token.size() >= 2 && token.front() == '"' && token.back() == '"') {
                            cfg.extra_vst2_paths.push_back(
                                token.substr(1, token.size() - 2));
                        }
                    }
                    in_paths_array = false;
                }
                continue;
            }
            if (in_paths_array) {
                if (t == "]") { in_paths_array = false; continue; }
                std::string v = trim(t);
                // Remove trailing comma
                if (!v.empty() && v.back() == ',') v.pop_back();
                v = trim(v);
                if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                    cfg.extra_vst2_paths.push_back(v.substr(1, v.size() - 2));
                }
            }
        }

        if (in_gui) {
            if (t.find("mac_mode") == 0) {
                size_t eq = t.find('=');
                if (eq != std::string::npos) {
                    std::string val = trim(t.substr(eq + 1));
                    if (!val.empty() && val.front() == '"') val = val.substr(1);
                    if (!val.empty() && val.back() == '"') val.pop_back();
                    cfg.mac_ui_mode = val;
                }
            }
            if (t.find("mac_attach_target") == 0) {
                size_t eq = t.find('=');
                if (eq != std::string::npos) {
                    std::string val = trim(t.substr(eq + 1));
                    if (!val.empty() && val.front() == '"') val = val.substr(1);
                    if (!val.empty() && val.back() == '"') val.pop_back();
                    cfg.mac_embed_attach_target = val;
                }
            }
        }

        // Isolation section
        if (in_isolation && !in_override) {
            if (t.find("default") == 0) {
                size_t eq = t.find('=');
                if (eq != std::string::npos) {
                    std::string val = trim(t.substr(eq + 1));
                    if (val.front() == '"') val = val.substr(1);
                    if (val.back() == '"') val.pop_back();
                    cfg.isolation_default = val;
                }
            }
        }
        if (in_override) {
            size_t eq = t.find('=');
            if (eq != std::string::npos) {
                std::string key = trim(t.substr(0, eq));
                std::string val = trim(t.substr(eq + 1));
                if (val.front() == '"') val = val.substr(1);
                if (val.back() == '"') val.pop_back();
                if (key == "match") current_override.match = val;
                if (key == "mode") current_override.mode = val;
            }
        }
        // Expose section
        if (in_expose && !in_whitelist_entry) {
            if (t.find("mode") == 0) {
                size_t eq = t.find('=');
                if (eq != std::string::npos) {
                    std::string val = trim(t.substr(eq + 1));
                    if (val.front() == '"') val = val.substr(1);
                    if (val.back() == '"') val.pop_back();
                    cfg.expose_mode = val;
                }
            }
            if (t.find("vst2_bridged") == 0) {
                cfg.expose_vst2_bridged = parse_toml_bool(t, cfg.expose_vst2_bridged);
            }
            if (t.find("vst2_native") == 0) {
                cfg.expose_vst2_native = parse_toml_bool(t, cfg.expose_vst2_native);
            }
            if (t.find("vst3") == 0) {
                cfg.expose_vst3 = parse_toml_bool(t, cfg.expose_vst3);
            }
            if (t.find("au") == 0) {
                cfg.expose_au = parse_toml_bool(t, cfg.expose_au);
            }
        }
        if (in_whitelist_entry) {
            size_t eq = t.find('=');
            if (eq != std::string::npos) {
                std::string key = trim(t.substr(0, eq));
                std::string val = trim(t.substr(eq + 1));
                if (val.front() == '"') val = val.substr(1);
                if (val.back() == '"') val.pop_back();
                if (key == "path") current_wl.path = val;
            }
        }
    }

    // Flush last entries
    if (in_override && !current_override.match.empty())
        cfg.isolation_overrides.push_back(current_override);
    if (in_whitelist_entry && !current_wl.path.empty())
        cfg.whitelist.push_back(current_wl);

    if (cfg.expose_mode == "all") {
        cfg.expose_vst2_bridged = true;
        cfg.expose_vst2_native = true;
        cfg.expose_vst3 = true;
        cfg.expose_au = true;
    }

    fprintf(stderr, "keepsake: loaded config (expose=%s bridged-vst2=%s native-vst2=%s vst3=%s au=%s, %zu whitelist, %zu extra paths, replace-default-vst2-paths=%s, mac-ui=%s, mac-attach=%s, isolation=%s)\n",
            cfg.expose_mode.c_str(),
            cfg.expose_vst2_bridged ? "true" : "false",
            cfg.expose_vst2_native ? "true" : "false",
            cfg.expose_vst3 ? "true" : "false",
            cfg.expose_au ? "true" : "false",
            cfg.whitelist.size(),
            cfg.extra_vst2_paths.size(),
            cfg.replace_default_vst2_paths ? "true" : "false",
            cfg.mac_ui_mode.c_str(),
            cfg.mac_embed_attach_target.c_str(),
            cfg.isolation_default.c_str());
    return cfg;
}

int64_t file_mtime(const std::string &path) {
    std::error_code ec;
    auto t = fs::last_write_time(path, ec);
    if (ec) return 0;
    auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
        t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return sctp.time_since_epoch().count();
}
