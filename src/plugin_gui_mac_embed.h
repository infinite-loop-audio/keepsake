#pragma once

#ifdef __APPLE__

struct KeepsakePlugin;
#include <string>
#include <clap/ext/gui.h>
#include "ipc.h"

std::string gui_mac_ui_mode();
bool gui_mac_mode_uses_iosurface_preview();
bool gui_mac_mode_prefers_live_editor();
bool gui_mac_should_use_iosurface_embed();
std::string gui_mac_embed_attach_target();
bool gui_mac_attach_iosurface(KeepsakePlugin *kp,
                              const clap_window_t *window,
                              const IpcEditorSurface &surface);
void gui_mac_detach_iosurface(KeepsakePlugin *kp);
void gui_mac_refresh_iosurface(KeepsakePlugin *kp);

#endif
