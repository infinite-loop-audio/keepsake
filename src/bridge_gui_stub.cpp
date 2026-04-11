//
// Bridge GUI — stub for platforms without full GUI support yet.
// On Windows and Linux, embedded mode IS supported via SetParent / XReparentWindow.
//

#include "bridge_gui.h"
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>

// --- Windows embedded editor via SetParent ---

static HWND g_editor_hwnd = nullptr;
static HWND g_parent_hwnd = nullptr;
static BridgeLoader *g_active_loader = nullptr;
static bool g_editor_open = false;
static UINT_PTR g_idle_timer = 0;

static LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        if (g_active_loader) g_active_loader->editor_idle();
        return 0;
    case WM_CLOSE:
        if (g_active_loader) g_active_loader->close_editor();
        g_editor_open = false;
        DestroyWindow(hwnd);
        g_editor_hwnd = nullptr;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void gui_init() {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = EditorWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "KeepsakeEditor";
    RegisterClassA(&wc);
}

bool gui_open_editor(BridgeLoader *loader, const EditorHeaderInfo &header) {
    if (!loader || !loader->has_editor()) return false;
    if (g_editor_open) return true;

    int w = 640, h = 480;
    loader->get_editor_rect(w, h);

    g_editor_hwnd = CreateWindowExA(
        WS_EX_TOOLWINDOW, "KeepsakeEditor", "Keepsake",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, w, h,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!g_editor_hwnd) return false;

    loader->open_editor(static_cast<void *>(g_editor_hwnd));
    g_active_loader = loader;
    g_editor_open = true;

    ShowWindow(g_editor_hwnd, SW_SHOW);
    g_idle_timer = SetTimer(g_editor_hwnd, 1, 16, nullptr); // ~60fps
    return true;
}

bool gui_open_editor_embedded(BridgeLoader *loader, uint64_t native_handle) {
    if (!loader || !loader->has_editor()) return false;
    if (g_editor_open) return true;

    HWND parent = reinterpret_cast<HWND>(static_cast<uintptr_t>(native_handle));
    if (!IsWindow(parent)) return false;

    int w = 640, h = 480;
    loader->get_editor_rect(w, h);

    // Create a child window inside the host's parent
    g_editor_hwnd = CreateWindowExA(
        0, "KeepsakeEditor", nullptr,
        WS_CHILD | WS_VISIBLE,
        0, 0, w, h,
        parent, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!g_editor_hwnd) return false;

    loader->open_editor(static_cast<void *>(g_editor_hwnd));
    g_active_loader = loader;
    g_parent_hwnd = parent;
    g_editor_open = true;

    g_idle_timer = SetTimer(g_editor_hwnd, 1, 16, nullptr);
    fprintf(stderr, "bridge: editor embedded in host window %p\n",
            static_cast<void *>(parent));
    return true;
}

void gui_close_editor(BridgeLoader *loader) {
    if (!g_editor_open) return;
    if (g_idle_timer) { KillTimer(g_editor_hwnd, g_idle_timer); g_idle_timer = 0; }
    if (loader) loader->close_editor();
    else if (g_active_loader) g_active_loader->close_editor();
    if (g_editor_hwnd) { DestroyWindow(g_editor_hwnd); g_editor_hwnd = nullptr; }
    g_active_loader = nullptr;
    g_parent_hwnd = nullptr;
    g_editor_open = false;
}

bool gui_get_editor_rect(BridgeLoader *loader, int &w, int &h) {
    return loader ? loader->get_editor_rect(w, h) : false;
}

void gui_idle(BridgeLoader *) {
    if (!g_editor_open) return;
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

bool gui_is_open() { return g_editor_open; }

#elif defined(__linux__)
// --- Linux embedded editor via X11 ---
// Requires X11 headers — linked via -lX11

#include <X11/Xlib.h>
#include <dlfcn.h>

static Display *g_display = nullptr;
static Window g_editor_window = 0;
static Window g_parent_window = 0;
static BridgeLoader *g_active_loader = nullptr;
static bool g_editor_open = false;

void gui_init() {
    g_display = XOpenDisplay(nullptr);
    if (!g_display) {
        fprintf(stderr, "bridge: failed to open X11 display\n");
    }
}

bool gui_open_editor(BridgeLoader *loader, const EditorHeaderInfo &) {
    if (!loader || !loader->has_editor() || !g_display) return false;
    if (g_editor_open) return true;

    int w = 640, h = 480;
    loader->get_editor_rect(w, h);

    int screen = DefaultScreen(g_display);
    g_editor_window = XCreateSimpleWindow(
        g_display, RootWindow(g_display, screen),
        200, 200, w, h, 1,
        BlackPixel(g_display, screen),
        WhitePixel(g_display, screen));

    XMapWindow(g_display, g_editor_window);
    XFlush(g_display);

    loader->open_editor(reinterpret_cast<void *>(g_editor_window));
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

    // Create child window in the host's parent
    g_editor_window = XCreateSimpleWindow(
        g_display, parent,
        0, 0, w, h, 0, 0, 0);

    XMapWindow(g_display, g_editor_window);
    XFlush(g_display);

    loader->open_editor(reinterpret_cast<void *>(g_editor_window));
    g_active_loader = loader;
    g_parent_window = parent;
    g_editor_open = true;

    fprintf(stderr, "bridge: editor embedded in X11 window 0x%lx\n", parent);
    return true;
}

void gui_close_editor(BridgeLoader *loader) {
    if (!g_editor_open) return;
    if (loader) loader->close_editor();
    else if (g_active_loader) g_active_loader->close_editor();
    if (g_display && g_editor_window) {
        XDestroyWindow(g_display, g_editor_window);
        g_editor_window = 0;
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
    }
    if (loader) loader->editor_idle();
    else if (g_active_loader) g_active_loader->editor_idle();
}

bool gui_is_open() { return g_editor_open; }

#else
// --- Fallback: no GUI ---

void gui_init() {}
bool gui_open_editor(BridgeLoader *, const EditorHeaderInfo &) { return false; }
bool gui_open_editor_embedded(BridgeLoader *, uint64_t) { return false; }
void gui_close_editor(BridgeLoader *) {}
bool gui_get_editor_rect(BridgeLoader *l, int &w, int &h) {
    return l ? l->get_editor_rect(w, h) : false;
}
void gui_idle(BridgeLoader *) {}
bool gui_is_open() { return false; }

#endif
