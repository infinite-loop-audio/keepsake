#pragma once
//
// Internal helpers shared across plugin_*.cpp files.
//

#include "plugin.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

#ifndef _WIN32
#include <sched.h>
#endif

// Get the KeepsakePlugin from a clap_plugin_t pointer.
inline KeepsakePlugin *get(const clap_plugin_t *plugin) {
    return reinterpret_cast<KeepsakePlugin *>(plugin->plugin_data);
}

// Send a command and wait for OK/ERROR response.
bool send_and_wait(KeepsakePlugin *kp, uint32_t opcode,
                    const void *payload = nullptr,
                    uint32_t size = 0,
                    std::vector<uint8_t> *ok_payload = nullptr);
