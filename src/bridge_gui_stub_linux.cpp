//
// Bridge GUI — Linux stub/editor hosting.
//

#include "bridge_gui.h"

#ifdef __linux__

#include <X11/Xlib.h>
#include <cstdio>
#include <cstring>

static Display *g_display = nullptr;
static Window g_editor_window = 0;
static Window g_parent_window = 0;
static Window g_header_window = 0;
static GC g_header_gc = nullptr;
static BridgeLoader *g_active_loader = nullptr;
static bool g_editor_open = false;
static EditorHeaderInfo g_header_info_x11;
static const int X11_HEADER_HEIGHT = 24;

static void x11_draw_header() {
    if (!g_display || !g_header_window || !g_header_gc) return;

    XSetForeground(g_display, g_header_gc, 0x262626);
    XFillRectangle(g_display, g_header_window, g_header_gc,
                   0, 0, 2000, X11_HEADER_HEIGHT);

    XSetForeground(g_display, g_header_gc, 0xDDDDDD);
    char text[512];
    snprintf(text, sizeof(text), "  %s   [%s]  [%s]  [%s]",
             g_header_info_x11.plugin_name.c_str(),
             g_header_info_x11.format.c_str(),
             g_header_info_x11.architecture.c_str(),
             g_header_info_x11.isolation.c_str());
    XDrawString(g_display, g_header_window, g_header_gc,
                8, 16, text, static_cast<int>(strlen(text)));
}

void gui_init() {
    g_display = XOpenDisplay(nullptr);
    if (!g_display) fprintf(stderr, "bridge: failed to open X11 display\n");
}

bool gui_open_editor(BridgeLoader *loader, const EditorHeaderInfo &header) {
    if (!loader || !loader->has_editor() || !g_display) return false;
    if (g_editor_open) return true;

    g_header_info_x11 = header;
    int w = 640, h = 480;
    loader->get_editor_rect(w, h);

    int screen = DefaultScreen(g_display);
    Window root = RootWindow(g_display, screen);
    g_editor_window = XCreateSimpleWindow(
        g_display, root, 200, 200, w, h + X11_HEADER_HEIGHT, 1,
        BlackPixel(g_display, screen), 0x262626);

    char title[256];
    snprintf(title, sizeof(title), "Keepsake — %s", header.plugin_name.c_str());
    XStoreName(g_display, g_editor_window, title);

    g_header_window = XCreateSimpleWindow(
        g_display, g_editor_window, 0, 0, w, X11_HEADER_HEIGHT, 0, 0, 0x262626);
    XMapWindow(g_display, g_header_window);
    XSelectInput(g_display, g_header_window, ExposureMask);

    g_header_gc = XCreateGC(g_display, g_header_window, 0, nullptr);

    Window editor_area = XCreateSimpleWindow(
        g_display, g_editor_window, 0, X11_HEADER_HEIGHT, w, h, 0,
        0, WhitePixel(g_display, screen));
    XMapWindow(g_display, editor_area);

    XMapWindow(g_display, g_editor_window);
    XFlush(g_display);

    x11_draw_header();

    loader->open_editor(reinterpret_cast<void *>(editor_area));
    g_active_loader = loader;
    g_editor_open = true;
    return true;
}

bool gui_open_editor_embedded(BridgeLoader *loader, uint64_t native_handle) {
    if (!loader || !loader->has_editor() || !g_display) return false;
    if (g_editor_open) return true;

    Window parent = static_cast<Window>(native_handle);
    int w = 640, h = 480;
    loader->get_editor_rect(w, h);

    g_editor_window = XCreateSimpleWindow(g_display, parent, 0, 0, w, h, 0, 0, 0);
    XMapWindow(g_display, g_editor_window);
    XFlush(g_display);

    loader->open_editor(reinterpret_cast<void *>(g_editor_window));
    g_active_loader = loader;
    g_parent_window = parent;
    g_editor_open = true;

    fprintf(stderr, "bridge: editor embedded in X11 window 0x%lx\n", parent);
    return true;
}

void gui_set_editor_transient(uint64_t) {}

void gui_close_editor(BridgeLoader *loader) {
    if (!g_editor_open) return;
    if (loader) loader->close_editor();
    else if (g_active_loader) g_active_loader->close_editor();
    if (g_display) {
        if (g_header_gc) {
            XFreeGC(g_display, g_header_gc);
            g_header_gc = nullptr;
        }
        if (g_editor_window) {
            XDestroyWindow(g_display, g_editor_window);
            g_editor_window = 0;
        }
        g_header_window = 0;
    }
    g_active_loader = nullptr;
    g_parent_window = 0;
    g_editor_open = false;
}

bool gui_get_editor_rect(BridgeLoader *loader, int &w, int &h) {
    return loader ? loader->get_editor_rect(w, h) : false;
}

void gui_idle(BridgeLoader *loader) {
    if (!g_editor_open || !g_display) return;
    while (XPending(g_display)) {
        XEvent ev;
        XNextEvent(g_display, &ev);
        if (ev.type == Expose && ev.xexpose.window == g_header_window) {
            x11_draw_header();
        }
    }
    if (loader) loader->editor_idle();
    else if (g_active_loader) g_active_loader->editor_idle();
}

bool gui_is_open() { return g_editor_open; }
uint32_t gui_open_editor_iosurface(BridgeLoader *, int, int) { return 0; }
void gui_forward_mouse(const IpcMouseEvent &) {}
void gui_forward_key(const IpcKeyEvent &) {}
bool gui_is_iosurface_mode() { return false; }

#endif
