#pragma once

#include <cstdint>
#include <string>

std::string keepsake_trim_copy(const std::string &text);
std::string keepsake_strip_known_arch_suffixes(std::string name);
std::string keepsake_format_label(uint32_t format);
std::string keepsake_display_arch_suffix(const std::string &arch);
std::string keepsake_make_display_name(const std::string &plugin_name,
                                       uint32_t format,
                                       const std::string &binary_arch);
