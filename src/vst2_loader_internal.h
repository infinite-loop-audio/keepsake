#pragma once

#include "ipc.h"
#include "vst2_loader.h"

#include <string>
#include <vector>

std::string vst2_filename_stem(const std::string &path);
bool vst2_bridge_info_is_sane(const Vst2PluginInfo &info);
bool vst2_parse_init_response(const std::vector<uint8_t> &payload,
                              Vst2PluginInfo &info);
