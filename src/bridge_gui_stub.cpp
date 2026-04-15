#include "bridge_gui.h"

#if !defined(_WIN32) && !defined(__linux__)
// --- Fallback: no GUI ---

void gui_init() {}
bool gui_open_editor(BridgeLoader *, const EditorHeaderInfo &) { return false; }
bool gui_open_editor_embedded(BridgeLoader *, uint64_t) { return false; }
bool gui_stage_editor_parent(BridgeLoader *, uint64_t) { return false; }
bool gui_has_pending_work() { return false; }
void gui_get_editor_status(bool &open, bool &pending) { open = false; pending = false; }
void gui_set_status_shm(void *) {}
void gui_set_editor_transient(uint64_t) {}
void gui_close_editor(BridgeLoader *) {}
bool gui_get_editor_rect(BridgeLoader *l, int &w, int &h) {
    return l ? l->get_editor_rect(w, h) : false;
}
void gui_idle(BridgeLoader *) {}
bool gui_is_open() { return false; }
uint32_t gui_open_editor_iosurface(BridgeLoader *, int, int) { return 0; }
void gui_forward_mouse(const IpcMouseEvent &) {}
void gui_forward_key(const IpcKeyEvent &) {}
bool gui_is_iosurface_mode() { return false; }

#endif
