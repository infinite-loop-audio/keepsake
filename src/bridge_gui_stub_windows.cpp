//
// Bridge GUI — Windows stub/editor hosting.
//

#include "bridge_gui.h"
#include "debug_log.h"

#ifdef _WIN32

#include <cstdio>
#include <cstring>
#include <windows.h>

static HWND g_editor_hwnd = nullptr;
static HWND g_parent_hwnd = nullptr;
static HWND g_header_hwnd = nullptr;
static BridgeLoader *g_active_loader = nullptr;
static bool g_editor_open = false;
static UINT_PTR g_idle_timer = 0;
static EditorHeaderInfo g_header_info;
static const int WIN_HEADER_HEIGHT = 24;

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

static LRESULT CALLBACK HeaderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        HBRUSH bg = CreateSolidBrush(RGB(38, 38, 38));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(220, 220, 220));
        HFONT font = CreateFontA(13, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
        HFONT old_font = (HFONT)SelectObject(hdc, font);

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

    g_header_info = header;
    int w = 640, h = 480;
    loader->get_editor_rect(w, h);

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

    char title[256];
    snprintf(title, sizeof(title), "Keepsake \xe2\x80\x94 %s", header.plugin_name.c_str());

    RECT wr = {0, 0, w, h + WIN_HEADER_HEIGHT};
    AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);

    g_editor_hwnd = CreateWindowExA(
        WS_EX_TOOLWINDOW, "KeepsakeEditor", title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!g_editor_hwnd) return false;

    g_header_hwnd = CreateWindowExA(
        0, "KeepsakeHeader", nullptr, WS_CHILD | WS_VISIBLE,
        0, 0, w, WIN_HEADER_HEIGHT,
        g_editor_hwnd, nullptr, GetModuleHandle(nullptr), nullptr);

    HWND editor_panel = CreateWindowExA(
        0, "KeepsakeEditor", nullptr, WS_CHILD | WS_VISIBLE,
        0, WIN_HEADER_HEIGHT, w, h,
        g_editor_hwnd, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!loader->open_editor(static_cast<void *>(editor_panel))) {
        keepsake_debug_log("bridge: floating editor open failed\n");
        DestroyWindow(editor_panel);
        DestroyWindow(g_header_hwnd);
        DestroyWindow(g_editor_hwnd);
        g_header_hwnd = nullptr;
        g_editor_hwnd = nullptr;
        return false;
    }
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

    g_editor_hwnd = CreateWindowExA(
        0, "KeepsakeEditor", nullptr,
        WS_CHILD | WS_VISIBLE,
        0, 0, w, h,
        parent, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!g_editor_hwnd) return false;

    keepsake_debug_log("bridge: editor embed parent=%p child=%p\n",
                       static_cast<void *>(parent), static_cast<void *>(g_editor_hwnd));
    if (!loader->open_editor(static_cast<void *>(g_editor_hwnd))) {
        keepsake_debug_log("bridge: embedded editor open failed\n");
        DestroyWindow(g_editor_hwnd);
        g_editor_hwnd = nullptr;
        return false;
    }
    g_active_loader = loader;
    g_parent_hwnd = parent;
    g_editor_open = true;

    g_idle_timer = SetTimer(g_editor_hwnd, 1, 16, nullptr);
    keepsake_debug_log("bridge: editor embedded in host window %p\n",
                       static_cast<void *>(parent));
    return true;
}

void gui_close_editor(BridgeLoader *loader) {
    if (!g_editor_open) return;
    if (g_idle_timer) {
        KillTimer(g_editor_hwnd, g_idle_timer);
        g_idle_timer = 0;
    }
    if (loader) loader->close_editor();
    else if (g_active_loader) g_active_loader->close_editor();
    if (g_editor_hwnd) {
        DestroyWindow(g_editor_hwnd);
        g_editor_hwnd = nullptr;
    }
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
uint32_t gui_open_editor_iosurface(BridgeLoader *, int, int) { return 0; }
void gui_forward_mouse(const IpcMouseEvent &) {}
void gui_forward_key(const IpcKeyEvent &) {}
bool gui_is_iosurface_mode() { return false; }

#endif
