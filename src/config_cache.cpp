//
// Keepsake scan cache persistence.
//

#include "config_internal.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

std::string config_cache_path() {
    return config_dir() + "/cache.dat";
}

std::string config_escape(const std::string &s) {
    std::string r;
    for (char c : s) {
        if (c == '\t') r += "\\t";
        else if (c == '\n') r += "\\n";
        else if (c == '\\') r += "\\\\";
        else r += c;
    }
    return r;
}

std::string config_unescape(const std::string &s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            if (s[i + 1] == 't') { r += '\t'; i++; }
            else if (s[i + 1] == 'n') { r += '\n'; i++; }
            else if (s[i + 1] == '\\') { r += '\\'; i++; }
            else r += s[i];
        } else {
            r += s[i];
        }
    }
    return r;
}

bool cache_entry_is_sane(const Vst2PluginInfo &p) {
    if (p.num_inputs < 0 || p.num_inputs > 64) return false;
    if (p.num_outputs < 0 || p.num_outputs > 64) return false;
    if (p.num_params < 0 || p.num_params > 100000) return false;
    if (p.category < 0 || p.category > 32) return false;
    if (p.format > FORMAT_AU) return false;
    return true;
}

void cache_save(const std::vector<Vst2PluginInfo> &plugins) {
    std::string dir = config_dir();
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::ofstream f(config_cache_path());
    if (!f.is_open()) return;

    for (const auto &p : plugins) {
        f << config_escape(p.file_path)
          << "\t" << p.unique_id
          << "\t" << config_escape(p.name)
          << "\t" << config_escape(p.vendor)
          << "\t" << p.vendor_version
          << "\t" << p.category
          << "\t" << p.flags
          << "\t" << p.num_inputs
          << "\t" << p.num_outputs
          << "\t" << p.num_params
          << "\t" << p.format
          << "\t" << (p.needs_cross_arch ? 1 : 0)
          << "\t" << file_mtime(p.file_path)
          << "\n";
    }

    fprintf(stderr, "keepsake: saved cache (%zu plugins) to '%s'\n",
            plugins.size(), config_cache_path().c_str());
}

std::vector<Vst2PluginInfo> cache_load() {
    std::vector<Vst2PluginInfo> result;
    std::ifstream f(config_cache_path());
    if (!f.is_open()) return result;

    std::string line;
    bool found_invalid = false;
    while (std::getline(f, line)) {
        if (line.empty()) continue;

        std::vector<std::string> fields;
        std::istringstream ss(line);
        std::string field;
        while (std::getline(ss, field, '\t')) fields.push_back(field);

        if (fields.size() < 11) continue;

        Vst2PluginInfo p{};
        p.file_path = config_unescape(fields[0]);
        p.unique_id = static_cast<int32_t>(std::stol(fields[1]));
        p.name = config_unescape(fields[2]);
        p.vendor = config_unescape(fields[3]);
        p.vendor_version = static_cast<int32_t>(std::stol(fields[4]));
        p.category = static_cast<int32_t>(std::stol(fields[5]));
        p.flags = static_cast<int32_t>(std::stol(fields[6]));
        p.num_inputs = static_cast<int32_t>(std::stol(fields[7]));
        p.num_outputs = static_cast<int32_t>(std::stol(fields[8]));
        size_t needs_cross_arch_idx = 9;
        size_t mtime_idx = 10;
        if (fields.size() >= 13) {
            p.num_params = static_cast<int32_t>(std::stol(fields[9]));
            p.format = static_cast<uint32_t>(std::stoul(fields[10]));
            needs_cross_arch_idx = 11;
            mtime_idx = 12;
        } else {
            p.format = FORMAT_VST2;
        }
        p.needs_cross_arch = (fields[needs_cross_arch_idx] == "1");
        int64_t cached_mtime = std::stoll(fields[mtime_idx]);

        if (!fs::exists(p.file_path)) continue;
        if (file_mtime(p.file_path) != cached_mtime) continue;
        if (!cache_entry_is_sane(p)) {
            fprintf(stderr,
                    "keepsake: ignoring corrupted cache entry '%s' (category=%d in=%d out=%d params=%d)\n",
                    p.file_path.c_str(), p.category, p.num_inputs,
                    p.num_outputs, p.num_params);
            found_invalid = true;
            continue;
        }

        result.push_back(std::move(p));
    }

    if (found_invalid) {
        fprintf(stderr, "keepsake: cache contained invalid metadata, forcing rescan\n");
        result.clear();
        return result;
    }

    fprintf(stderr, "keepsake: loaded cache (%zu valid plugins) from '%s'\n",
            result.size(), config_cache_path().c_str());
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
