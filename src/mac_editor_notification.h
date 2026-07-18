#pragma once

#ifdef __APPLE__

#include <cctype>
#include <string>

inline std::string mac_editor_state_notification_name(const std::string &shm_name) {
    std::string name = "audio.infiniteloop.keepsake.editor";
    name.reserve(name.size() + shm_name.size() + 1);
    for (const unsigned char ch : shm_name) {
        name.push_back(std::isalnum(ch) ? static_cast<char>(ch) : '.');
    }
    return name;
}

#endif
