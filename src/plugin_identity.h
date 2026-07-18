#pragma once

#include <cstdint>
#include <string>

std::string keepsake_make_plugin_id(uint32_t format, int32_t unique_id);
std::string keepsake_plugin_id_arch_suffix(const std::string &arch);
std::string keepsake_make_plugin_id_disambiguated(uint32_t format,
                                                   int32_t unique_id,
                                                   const std::string &arch,
                                                   const std::string &path,
                                                   bool needs_suffix);
