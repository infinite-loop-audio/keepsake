#pragma once

#ifdef __APPLE__

#include <clap/ext/gui.h>

struct KeepsakePlugin;

bool gui_mac_attach_placeholder(KeepsakePlugin *kp,
                                const clap_window_t *window);
void gui_mac_detach_placeholder(KeepsakePlugin *kp);
void gui_mac_update_placeholder(KeepsakePlugin *kp);
bool gui_mac_open_native_editor(KeepsakePlugin *kp);

#endif
