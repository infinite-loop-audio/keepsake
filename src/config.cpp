//
// Keepsake configuration and scan cache.
// Uses minimal hand-rolled serialization to avoid external dependencies.
//

#include "config.h"
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

KeepsakeConfig config_load() {
    KeepsakeConfig cfg;
    std::string path = config_dir() + "/config.toml";

    std::ifstream f(path);
    if (!f.is_open()) return cfg;

    std::string line;
    bool in_scan = false;
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
            continue;
        }
        if (t == "[isolation]") {
            in_isolation = true; in_scan = false; in_override = false;
            in_expose = false; in_whitelist_entry = false;
            continue;
        }
        if (t == "[expose]") {
            in_expose = true; in_scan = false; in_isolation = false;
            in_override = false; in_whitelist_entry = false;
            continue;
        }
        if (t == "[[expose.plugin]]") {
            if (in_whitelist_entry && !current_wl.path.empty())
                cfg.whitelist.push_back(current_wl);
            current_wl = {};
            in_whitelist_entry = true; in_expose = true;
            in_scan = false; in_isolation = false; in_override = false;
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
            in_isolation = false; in_override = false;
            continue;
        }

        if (in_scan) {
            // rescan = true/false
            if (t.find("rescan") == 0) {
                cfg.force_rescan = (t.find("true") != std::string::npos);
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

    fprintf(stderr, "keepsake: loaded config (expose=%s, %zu whitelist, %zu extra paths, isolation=%s)\n",
            cfg.expose_mode.c_str(), cfg.whitelist.size(),
            cfg.extra_vst2_paths.size(), cfg.isolation_default.c_str());
    return cfg;
}

// --- Scan cache (simple line-based format) ---
// Format per line: path\tfield=value\tfield=value\t...
// Simpler than JSON, no parser dependency.

static std::string cache_path() {
    return config_dir() + "/cache.dat";
}

static std::string escape(const std::string &s) {
    std::string r;
    for (char c : s) {
        if (c == '\t') r += "\\t";
        else if (c == '\n') r += "\\n";
        else if (c == '\\') r += "\\\\";
        else r += c;
    }
    return r;
}

static std::string unescape(const std::string &s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            if (s[i+1] == 't') { r += '\t'; i++; }
            else if (s[i+1] == 'n') { r += '\n'; i++; }
            else if (s[i+1] == '\\') { r += '\\'; i++; }
            else r += s[i];
        } else {
            r += s[i];
        }
    }
    return r;
}

void cache_save(const std::vector<Vst2PluginInfo> &plugins) {
    std::string dir = config_dir();
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::ofstream f(cache_path());
    if (!f.is_open()) return;

    for (const auto &p : plugins) {
        f << escape(p.file_path)
          << "\t" << p.unique_id
          << "\t" << escape(p.name)
          << "\t" << escape(p.vendor)
          << "\t" << p.vendor_version
          << "\t" << p.category
          << "\t" << p.flags
          << "\t" << p.num_inputs
          << "\t" << p.num_outputs
          << "\t" << (p.needs_cross_arch ? 1 : 0)
          << "\t" << file_mtime(p.file_path)
          << "\n";
    }

    fprintf(stderr, "keepsake: saved cache (%zu plugins) to '%s'\n",
            plugins.size(), cache_path().c_str());
}

std::vector<Vst2PluginInfo> cache_load() {
    std::vector<Vst2PluginInfo> result;
    std::ifstream f(cache_path());
    if (!f.is_open()) return result;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;

        // Split by tabs
        std::vector<std::string> fields;
        std::istringstream ss(line);
        std::string field;
        while (std::getline(ss, field, '\t')) {
            fields.push_back(field);
        }

        if (fields.size() < 11) continue;

        Vst2PluginInfo p;
        p.file_path = unescape(fields[0]);
        p.unique_id = static_cast<int32_t>(std::stol(fields[1]));
        p.name = unescape(fields[2]);
        p.vendor = unescape(fields[3]);
        p.vendor_version = static_cast<int32_t>(std::stol(fields[4]));
        p.category = static_cast<int32_t>(std::stol(fields[5]));
        p.flags = static_cast<int32_t>(std::stol(fields[6]));
        p.num_inputs = static_cast<int32_t>(std::stol(fields[7]));
        p.num_outputs = static_cast<int32_t>(std::stol(fields[8]));
        p.needs_cross_arch = (fields[9] == "1");
        int64_t cached_mtime = std::stoll(fields[10]);

        // Validate: file still exists and hasn't changed
        if (!fs::exists(p.file_path)) continue;
        if (file_mtime(p.file_path) != cached_mtime) continue;

        result.push_back(std::move(p));
    }

    fprintf(stderr, "keepsake: loaded cache (%zu valid plugins) from '%s'\n",
            result.size(), cache_path().c_str());
    return result;
}

bool cache_check_rescan_sentinel() {
    std::string sentinel = config_dir() + "/rescan";
    if (fs::exists(sentinel)) {
        std::error_code ec;
        fs::remove(sentinel, ec);
        fprintf(stderr, "keepsake: rescan sentinel found and removed\n");
        return true;
    }
    return false;
}

int64_t file_mtime(const std::string &path) {
    std::error_code ec;
    auto t = fs::last_write_time(path, ec);
    if (ec) return 0;
    auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
        t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return sctp.time_since_epoch().count();
}
