#pragma once

#include "config.h"
#include "ipc.h"

#include <string>

std::string config_cache_path();
std::string config_escape(const std::string &s);
std::string config_unescape(const std::string &s);
bool cache_entry_is_sane(const Vst2PluginInfo &p);
