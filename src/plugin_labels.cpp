#include "plugin_labels.h"

#include "ipc.h"

#include <cctype>

std::string keepsake_trim_copy(const std::string &text) {
    size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

static bool ends_with_case_insensitive(const std::string &text,
                                       const std::string &suffix) {
    if (suffix.size() > text.size()) return false;
    const size_t offset = text.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i) {
        unsigned char a = static_cast<unsigned char>(text[offset + i]);
        unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

std::string keepsake_strip_known_arch_suffixes(std::string name) {
    static const char *patterns[] = {
        " (64-bit)",
        " (32-bit)",
        " [64-bit]",
        " [32-bit]",
        " x64",
        " x86",
        "_x64",
        "_x86",
        "-x64",
        "-x86",
    };

    name = keepsake_trim_copy(name);
    bool removed = true;
    while (removed) {
        removed = false;
        for (const char *pattern : patterns) {
            const std::string suffix(pattern);
            if (!ends_with_case_insensitive(name, suffix)) continue;
            name.resize(name.size() - suffix.size());
            name = keepsake_trim_copy(name);
            removed = true;
            break;
        }
    }
    return name;
}

std::string keepsake_format_label(uint32_t format) {
    switch (format) {
    case FORMAT_VST2: return "VST2";
    case FORMAT_VST3: return "VST3";
    case FORMAT_AU: return "AU";
    default: return "Unknown";
    }
}

std::string keepsake_display_arch_suffix(const std::string &arch) {
    if (arch == "x86") return "x86";
    if (arch == "x86_64" || arch == "native") return "x64";
    if (arch == "arm64") return "arm64";
    return {};
}

std::string keepsake_make_display_name(const std::string &plugin_name,
                                       uint32_t format,
                                       const std::string &binary_arch) {
    std::string base = keepsake_strip_known_arch_suffixes(plugin_name);
    if (base.empty()) base = plugin_name;

    std::string suffix = keepsake_format_label(format);
    std::string arch = keepsake_display_arch_suffix(binary_arch);
    if (!arch.empty()) suffix += " " + arch;

    return base + " [" + suffix + "]";
}
