#import "bridge_gui_mac_internal.h"

NSWindow *g_window = nil;
NSWindow *g_parentless_plugin_window = nil;
NSView *g_header = nil;
NSView *g_editor_container = nil;
id g_frame_change_observer = nil;
BridgeLoader *g_active_loader = nil;
bool g_editor_open = false;
bool g_iosurface_mode = false;
int g_current_width = 0;
int g_current_height = 0;

void gui_init() {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp finishLaunching];
}

bool gui_open_editor(BridgeLoader *loader, const EditorHeaderInfo &header) {
    if (!loader || !loader->has_editor()) return false;
    if (g_editor_open) return true;
    return gui_open_windowed_editor(loader, header);
}

bool gui_open_editor_embedded(BridgeLoader * /*loader*/, uint64_t /*native_handle*/) {
    // macOS does not support cross-process NSView embedding.
    // Floating windows are the permanent approach on macOS.
    fprintf(stderr, "bridge: embedded editor not supported on macOS (use floating)\n");
    return false;
}

void gui_set_editor_transient(uint64_t) {}

void gui_close_editor(BridgeLoader *loader) {
    if (!g_editor_open) return;

    if (loader) loader->close_editor();
    else if (g_active_loader) g_active_loader->close_editor();

    gui_close_window_state();
    gui_close_iosurface_state();
    g_active_loader = nil;
    g_editor_open = false;
}

bool gui_get_editor_rect(BridgeLoader *loader, int &width, int &height) {
    if (!loader) return false;
    return loader->get_editor_rect(width, height);
}

void gui_idle(BridgeLoader *loader) {
    if (!g_editor_open) return;

    gui_pump_pending_events(nil);

    BridgeLoader *active = loader ? loader : g_active_loader;
    if (active) {
        active->editor_idle();
        gui_capture_iosurface_if_needed();
    }

    if (g_window && ![g_window isVisible]) {
        gui_close_editor(loader);
    }
}

bool gui_is_open() {
    return g_editor_open;
}
