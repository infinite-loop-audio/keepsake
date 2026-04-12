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

static const int WIN_HEADER_HEIGHT = 24;
static HWND g_header_hwnd = nullptr;
static EditorHeaderInfo g_header_info;

static LRESULT CALLBACK HeaderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        // Dark background
        HBRUSH bg = CreateSolidBrush(RGB(38, 38, 38));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        // Text
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(220, 220, 220));
        HFONT font = CreateFontA(13, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        HFONT old_font = (HFONT)SelectObject(hdc, font);

        // Plugin name + badges
        char text[512];
        snprintf(text, sizeof(text), "  %s   [%s]  [%s]  [%s]",
                 g_header_info.plugin_name.c_str(),
                 g_header_info.format.c_str(),
                 g_header_info.architecture.c_str(),
                 g_header_info.isolation.c_str());
        rc.left += 4;
        rc.top += 4;
        DrawTextA(hdc, text, -1, &rc, DT_LEFT | DT_SINGLELINE);

        SelectObject(hdc, old_font);
        DeleteObject(font);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

bool gui_open_editor(BridgeLoader *loader, const EditorHeaderInfo &header) {
    if (!loader || !loader->has_editor()) return false;
    if (g_editor_open) return true;

    g_header_info = header;
    int w = 640, h = 480;
    loader->get_editor_rect(w, h);

    // Register header class
    static bool header_registered = false;
    if (!header_registered) {
        WNDCLASSA wc = {};
        wc.lpfnWndProc = HeaderWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = "KeepsakeHeader";
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassA(&wc);
        header_registered = true;
    }

    // Title with plugin info
    char title[256];
    snprintf(title, sizeof(title), "Keepsake \xe2\x80\x94 %s",
             header.plugin_name.c_str());

    // Window sized for header + editor
    RECT wr = {0, 0, w, h + WIN_HEADER_HEIGHT};
    AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);

    g_editor_hwnd = CreateWindowExA(
        WS_EX_TOOLWINDOW, "KeepsakeEditor", title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!g_editor_hwnd) return false;

    // Header bar at top
    g_header_hwnd = CreateWindowExA(
        0, "KeepsakeHeader", nullptr, WS_CHILD | WS_VISIBLE,
        0, 0, w, WIN_HEADER_HEIGHT,
        g_editor_hwnd, nullptr, GetModuleHandle(nullptr), nullptr);

    // Editor panel below header — create a child for the plugin to draw into
    HWND editor_panel = CreateWindowExA(
        0, "KeepsakeEditor", nullptr, WS_CHILD | WS_VISIBLE,
        0, WIN_HEADER_HEIGHT, w, h,
        g_editor_hwnd, nullptr, GetModuleHandle(nullptr), nullptr);

    loader->open_editor(static_cast<void *>(editor_panel));
    g_active_loader = loader;
    g_editor_open = true;

    ShowWindow(g_editor_hwnd, SW_SHOW);
    g_idle_timer = SetTimer(g_editor_hwnd, 1, 16, nullptr);
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

static const int X11_HEADER_HEIGHT = 24;
static Window g_header_window = 0;
static GC g_header_gc = nullptr;
static EditorHeaderInfo g_header_info_x11;

static void x11_draw_header() {
    if (!g_display || !g_header_window || !g_header_gc) return;

    int screen = DefaultScreen(g_display);

    // Dark background
    XSetForeground(g_display, g_header_gc, 0x262626);
    XFillRectangle(g_display, g_header_window, g_header_gc,
                   0, 0, 2000, X11_HEADER_HEIGHT);

    // Text
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

bool gui_open_editor(BridgeLoader *loader, const EditorHeaderInfo &header) {
    if (!loader || !loader->has_editor() || !g_display) return false;
    if (g_editor_open) return true;

    g_header_info_x11 = header;
    int w = 640, h = 480;
    loader->get_editor_rect(w, h);

    int screen = DefaultScreen(g_display);

    // Main window (header + editor)
    Window root = RootWindow(g_display, screen);
    g_editor_window = XCreateSimpleWindow(
        g_display, root,
        200, 200, w, h + X11_HEADER_HEIGHT, 1,
        BlackPixel(g_display, screen),
        0x262626);

    // Set window title
    char title[256];
    snprintf(title, sizeof(title), "Keepsake — %s",
             header.plugin_name.c_str());
    XStoreName(g_display, g_editor_window, title);

    // Header bar (child window at top)
    g_header_window = XCreateSimpleWindow(
        g_display, g_editor_window,
        0, 0, w, X11_HEADER_HEIGHT, 0,
        0, 0x262626);
    XMapWindow(g_display, g_header_window);
    XSelectInput(g_display, g_header_window, ExposureMask);

    g_header_gc = XCreateGC(g_display, g_header_window, 0, nullptr);

    // Editor area (child window below header)
    Window editor_area = XCreateSimpleWindow(
        g_display, g_editor_window,
        0, X11_HEADER_HEIGHT, w, h, 0,
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
    if (g_display) {
        if (g_header_gc) { XFreeGC(g_display, g_header_gc); g_header_gc = nullptr; }
        if (g_editor_window) { XDestroyWindow(g_display, g_editor_window); g_editor_window = 0; }
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
uint32_t gui_open_editor_iosurface(BridgeLoader *, int, int) { return 0; }
void gui_forward_mouse(const IpcMouseEvent &) {}
void gui_forward_key(const IpcKeyEvent &) {}
bool gui_is_iosurface_mode() { return false; }

#endif
