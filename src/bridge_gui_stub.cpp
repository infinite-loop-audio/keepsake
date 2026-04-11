//
// Bridge GUI — stub for platforms without GUI support yet.
//

#include "bridge_gui.h"
#include <cstdio>

void gui_init() {}

bool gui_open_editor(BridgeLoader *, const EditorHeaderInfo &) {
    fprintf(stderr, "bridge: GUI not implemented on this platform\n");
    return false;
}

void gui_close_editor(BridgeLoader *) {}

bool gui_get_editor_rect(BridgeLoader *loader, int &w, int &h) {
    return loader ? loader->get_editor_rect(w, h) : false;
}

void gui_idle(BridgeLoader *) {}

bool gui_is_open() { return false; }
