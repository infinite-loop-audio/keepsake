//
// Bridge GUI — Windows stub/editor hosting.
//

#include "bridge_gui.h"
#include "debug_log.h"

#ifdef _WIN32

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <future>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <windows.h>

static LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK HeaderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static HWND g_editor_hwnd = nullptr;
static HWND g_parent_hwnd = nullptr;
static HWND g_owner_hwnd = nullptr;
static HWND g_header_hwnd = nullptr;
static HWND g_editor_panel_hwnd = nullptr;
static BridgeLoader *g_active_loader = nullptr;
static UINT_PTR g_idle_timer = 0;
static EditorHeaderInfo g_header_info;
static const int WIN_HEADER_HEIGHT = 24;
static int g_last_parent_w = 0;
static int g_last_parent_h = 0;
static std::atomic<bool> g_editor_open{false};
static const DWORD EMBED_OPEN_TIMEOUT_MS = 1500;
static constexpr const char *kKeepsakeHostWindowClass = "KeepsakeHostWindow";
static constexpr const char *kKeepsakeEmbedWrapperClass = "KeepsakeEmbedWrapper";
static constexpr const char *kKeepsakeEmbedPanelClass = "KeepsakeEmbedPanel";
static constexpr DWORD kKeepsakeEmbedChildStyle =
    WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
static constexpr DWORD kKeepsakeEmbedWrapperExStyle = WS_EX_CONTROLPARENT;
static constexpr UINT WM_KEEPSAKE_GUI_TASK = WM_APP + 41;

static std::thread g_gui_thread;
static DWORD g_gui_thread_id = 0;
static std::mutex g_gui_mutex;
static std::condition_variable g_gui_ready_cv;
static bool g_gui_ready = false;
static std::queue<std::function<void()>> g_gui_tasks;

static void log_window_info(const char *label, HWND hwnd) {
    if (!hwnd) {
        keepsake_debug_log("bridge: %s hwnd=(null)\n", label);
        return;
    }
    char klass[128] = {};
    GetClassNameA(hwnd, klass, sizeof(klass));
    RECT rc = {};
    GetWindowRect(hwnd, &rc);
    LONG_PTR style = GetWindowLongPtrA(hwnd, GWL_STYLE);
    LONG_PTR exstyle = GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
    HWND parent = GetParent(hwnd);
    HWND owner = GetWindow(hwnd, GW_OWNER);
    keepsake_debug_log(
        "bridge: %s hwnd=%p class='%s' style=0x%llx exstyle=0x%llx parent=%p owner=%p visible=%d rect=%ld,%ld,%ld,%ld\n",
        label,
        static_cast<void *>(hwnd),
        klass,
        static_cast<long long>(style),
        static_cast<long long>(exstyle),
        static_cast<void *>(parent),
        static_cast<void *>(owner),
        IsWindowVisible(hwnd) ? 1 : 0,
        static_cast<long>(rc.left),
        static_cast<long>(rc.top),
        static_cast<long>(rc.right),
        static_cast<long>(rc.bottom));
}

static BOOL CALLBACK LogChildWindowsProc(HWND hwnd, LPARAM lParam) {
    const char *label = reinterpret_cast<const char *>(lParam);
    log_window_info(label, hwnd);
    return TRUE;
}

static BOOL CALLBACK LogThreadWindowsProc(HWND hwnd, LPARAM) {
    log_window_info("thread-window", hwnd);
    EnumChildWindows(hwnd, LogChildWindowsProc, reinterpret_cast<LPARAM>("thread-child"));
    return TRUE;
}

static void log_window_tree(const char *phase, HWND parent, HWND wrapper, HWND panel) {
    keepsake_debug_log("bridge: window-tree phase=%s\n", phase);
    log_window_info("host-parent", parent);
    log_window_info("wrapper", wrapper);
    log_window_info("panel", panel);
    if (parent) EnumChildWindows(parent, LogChildWindowsProc, reinterpret_cast<LPARAM>("host-child"));
    DWORD tid = GetCurrentThreadId();
    keepsake_debug_log("bridge: window-tree thread=%lu\n", static_cast<unsigned long>(tid));
    EnumThreadWindows(tid, LogThreadWindowsProc, 0);
}

static void gui_register_classes() {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = EditorWndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = kKeepsakeHostWindowClass;
    RegisterClassA(&wc);

    WNDCLASSA wrapper_wc = {};
    wrapper_wc.lpfnWndProc = EditorWndProc;
    wrapper_wc.hInstance = GetModuleHandle(nullptr);
    wrapper_wc.lpszClassName = kKeepsakeEmbedWrapperClass;
    RegisterClassA(&wrapper_wc);

    WNDCLASSA panel_wc = {};
    panel_wc.lpfnWndProc = EditorWndProc;
    panel_wc.hInstance = GetModuleHandle(nullptr);
    panel_wc.lpszClassName = kKeepsakeEmbedPanelClass;
    RegisterClassA(&panel_wc);

    WNDCLASSA header_wc = {};
    header_wc.lpfnWndProc = HeaderWndProc;
    header_wc.hInstance = GetModuleHandle(nullptr);
    header_wc.lpszClassName = "KeepsakeHeader";
    header_wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassA(&header_wc);
}

static void gui_drain_tasks() {
    std::queue<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(g_gui_mutex);
        tasks.swap(g_gui_tasks);
    }
    while (!tasks.empty()) {
        auto task = std::move(tasks.front());
        tasks.pop();
        task();
    }
}

template <typename Fn>
static auto gui_call_sync(Fn &&fn) -> decltype(fn()) {
    using Result = decltype(fn());
    if (GetCurrentThreadId() == g_gui_thread_id) return fn();

    auto promise = std::make_shared<std::promise<Result>>();
    auto future = promise->get_future();
    {
        std::lock_guard<std::mutex> lock(g_gui_mutex);
        g_gui_tasks.push([promise, fn = std::forward<Fn>(fn)]() mutable {
            try {
                if constexpr (std::is_void_v<Result>) {
                    fn();
                    promise->set_value();
                } else {
                    promise->set_value(fn());
                }
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
    }
    PostThreadMessageA(g_gui_thread_id, WM_KEEPSAKE_GUI_TASK, 0, 0);
    return future.get();
}

static void position_and_show_floating_window(HWND hwnd) {
    if (!hwnd) return;

    RECT wr = {};
    if (!GetWindowRect(hwnd, &wr)) return;

    RECT work = {};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);

    const int w = wr.right - wr.left;
    const int h = wr.bottom - wr.top;
    const int x = work.left + ((work.right - work.left) - w) / 2;
    const int y = work.top + ((work.bottom - work.top) - h) / 2;

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    SetWindowPos(hwnd, HWND_NOTOPMOST, x, y, w, h, SWP_SHOWWINDOW);
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    keepsake_debug_log("bridge: floating editor window=%p pos=%d,%d size=%dx%d visible=%d\n",
                       static_cast<void *>(hwnd), x, y, w, h,
                       IsWindowVisible(hwnd) ? 1 : 0);
}

static void update_floating_owner() {
    if (!g_editor_hwnd || g_parent_hwnd) return;
    HWND owner = IsWindow(g_owner_hwnd) ? g_owner_hwnd : nullptr;
    SetWindowLongPtrA(g_editor_hwnd, GWLP_HWNDPARENT,
                      reinterpret_cast<LONG_PTR>(owner));
    keepsake_debug_log("bridge: floating editor owner=%p valid=%d\n",
                       static_cast<void *>(owner), owner ? 1 : 0);
}

static void settle_floating_window(BridgeLoader *loader) {
    if (!g_editor_hwnd) return;
    for (int i = 0; i < 12; ++i) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (loader) loader->editor_idle();
        if (g_editor_panel_hwnd) {
            ShowWindow(g_editor_panel_hwnd, SW_SHOW);
            UpdateWindow(g_editor_panel_hwnd);
        }
        position_and_show_floating_window(g_editor_hwnd);
        Sleep(16);
    }

    keepsake_debug_log("bridge: floating settle host_visible=%d panel=%p panel_visible=%d\n",
                       IsWindowVisible(g_editor_hwnd) ? 1 : 0,
                       static_cast<void *>(g_editor_panel_hwnd),
                       (g_editor_panel_hwnd && IsWindowVisible(g_editor_panel_hwnd)) ? 1 : 0);
}

static void resize_embedded_editor_to_parent() {
    if (!g_parent_hwnd || !g_editor_hwnd) return;
    RECT rc = {};
    if (!GetClientRect(g_parent_hwnd, &rc)) return;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;
    if (w == g_last_parent_w && h == g_last_parent_h) return;
    g_last_parent_w = w;
    g_last_parent_h = h;
    if (g_editor_panel_hwnd) {
        MoveWindow(g_editor_hwnd, 0, 0, w, h, TRUE);
        MoveWindow(g_editor_panel_hwnd, 0, 0, w, h, TRUE);
    } else {
        MoveWindow(g_editor_hwnd, 0, 0, w, h, TRUE);
    }
}

static LRESULT CALLBACK EditorWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        resize_embedded_editor_to_parent();
        if (g_active_loader) g_active_loader->editor_idle();
        return 0;
    case WM_CLOSE:
        if (g_active_loader) g_active_loader->close_editor();
        g_editor_open = false;
        g_editor_panel_hwnd = nullptr;
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
    std::unique_lock<std::mutex> lock(g_gui_mutex);
    if (g_gui_ready) return;

    g_gui_thread = std::thread([]() {
        g_gui_thread_id = GetCurrentThreadId();
        MSG msg;
        PeekMessageA(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        gui_register_classes();
        {
            std::lock_guard<std::mutex> ready_lock(g_gui_mutex);
            g_gui_ready = true;
        }
        g_gui_ready_cv.notify_all();

        while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_KEEPSAKE_GUI_TASK) {
                gui_drain_tasks();
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            gui_drain_tasks();
        }
    });
    g_gui_thread.detach();
    g_gui_ready_cv.wait(lock, [] { return g_gui_ready; });
}

static bool gui_open_editor_impl(BridgeLoader *loader, const EditorHeaderInfo &header) {
    if (!loader || !loader->has_editor()) return false;
    if (g_editor_open) return true;

    g_header_info = header;
    int w = 640, h = 480;
    loader->get_editor_rect(w, h);

    char title[256];
    snprintf(title, sizeof(title), "Keepsake \xe2\x80\x94 %s", header.plugin_name.c_str());

    RECT wr = {0, 0, w, h + WIN_HEADER_HEIGHT};
    AdjustWindowRect(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);

    g_editor_hwnd = CreateWindowExA(
        WS_EX_APPWINDOW, kKeepsakeHostWindowClass, title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        g_owner_hwnd, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!g_editor_hwnd) return false;

    update_floating_owner();

    keepsake_debug_log("bridge: floating host window=%p title='%s'\n",
                       static_cast<void *>(g_editor_hwnd), title);

    g_header_hwnd = CreateWindowExA(
        0, "KeepsakeHeader", nullptr, WS_CHILD | WS_VISIBLE,
        0, 0, w, WIN_HEADER_HEIGHT,
        g_editor_hwnd, nullptr, GetModuleHandle(nullptr), nullptr);

    HWND editor_panel = CreateWindowExA(
        kKeepsakeEmbedWrapperExStyle, kKeepsakeEmbedPanelClass, nullptr,
        kKeepsakeEmbedChildStyle | WS_VISIBLE,
        0, WIN_HEADER_HEIGHT, w, h,
        g_editor_hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
    g_editor_panel_hwnd = editor_panel;

    if (!loader->open_editor(static_cast<void *>(editor_panel))) {
        keepsake_debug_log("bridge: floating editor open failed\n");
        DestroyWindow(editor_panel);
        DestroyWindow(g_header_hwnd);
        DestroyWindow(g_editor_hwnd);
        g_header_hwnd = nullptr;
        g_editor_panel_hwnd = nullptr;
        g_editor_hwnd = nullptr;
        return false;
    }
    g_active_loader = loader;
    g_editor_open = true;

    position_and_show_floating_window(g_editor_hwnd);
    g_idle_timer = SetTimer(g_editor_hwnd, 1, 16, nullptr);
    settle_floating_window(loader);
    keepsake_debug_log("bridge: floating editor open succeeded panel=%p timer=%u\n",
                       static_cast<void *>(editor_panel),
                       static_cast<unsigned>(g_idle_timer));
    return true;
}

static bool gui_open_editor_embedded_impl(BridgeLoader *loader, uint64_t native_handle) {
    if (!loader || !loader->has_editor()) return false;
    if (g_editor_open) return true;

    HWND parent = reinterpret_cast<HWND>(static_cast<uintptr_t>(native_handle));
    if (!IsWindow(parent)) return false;

    int w = 640, h = 480;
    loader->get_editor_rect(w, h);

    g_editor_hwnd = CreateWindowExA(
        kKeepsakeEmbedWrapperExStyle, kKeepsakeEmbedWrapperClass, nullptr,
        kKeepsakeEmbedChildStyle,
        0, 0, w, h,
        parent, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!g_editor_hwnd) return false;

    HWND editor_panel = CreateWindowExA(
        0, kKeepsakeEmbedPanelClass, nullptr,
        kKeepsakeEmbedChildStyle,
        0, 0, w, h,
        g_editor_hwnd, nullptr, GetModuleHandle(nullptr), nullptr);

    if (!editor_panel) {
        DestroyWindow(g_editor_hwnd);
        g_editor_hwnd = nullptr;
        return false;
    }

    g_editor_panel_hwnd = editor_panel;
    g_active_loader = loader;
    g_parent_hwnd = parent;
    g_last_parent_w = 0;
    g_last_parent_h = 0;
    g_idle_timer = SetTimer(g_editor_hwnd, 1, 16, nullptr);
    log_window_tree("before-embed-open", parent, g_editor_hwnd, editor_panel);

    keepsake_debug_log("bridge: editor embed parent=%p child=%p\n",
                       static_cast<void *>(parent), static_cast<void *>(g_editor_hwnd));

    std::atomic<bool> open_done{false};
    std::atomic<bool> open_ok{false};
    std::thread open_thread([&]() {
        open_ok.store(loader->open_editor(static_cast<void *>(editor_panel)),
                      std::memory_order_release);
        open_done.store(true, std::memory_order_release);
    });

    DWORD start = GetTickCount();
    while (!open_done.load(std::memory_order_acquire)) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        loader->editor_idle();
        Sleep(16);
        if (GetTickCount() - start >= EMBED_OPEN_TIMEOUT_MS) {
            keepsake_debug_log("bridge: embedded editor open timed out after %lums\n",
                               static_cast<unsigned long>(EMBED_OPEN_TIMEOUT_MS));
            log_window_tree("embed-timeout", parent, g_editor_hwnd, editor_panel);
            open_thread.detach();
            if (g_idle_timer) {
                KillTimer(g_editor_hwnd, g_idle_timer);
                g_idle_timer = 0;
            }
            DestroyWindow(editor_panel);
            DestroyWindow(g_editor_hwnd);
            g_editor_panel_hwnd = nullptr;
            g_editor_hwnd = nullptr;
            g_active_loader = nullptr;
            g_parent_hwnd = nullptr;
            return false;
        }
    }

    open_thread.join();
    if (!open_ok.load(std::memory_order_acquire)) {
        keepsake_debug_log("bridge: embedded editor open failed\n");
        if (g_idle_timer) {
            KillTimer(g_editor_hwnd, g_idle_timer);
            g_idle_timer = 0;
        }
        DestroyWindow(editor_panel);
        DestroyWindow(g_editor_hwnd);
        g_editor_panel_hwnd = nullptr;
        g_editor_hwnd = nullptr;
        g_active_loader = nullptr;
        g_parent_hwnd = nullptr;
        return false;
    }

    g_editor_open = true;

    resize_embedded_editor_to_parent();
    ShowWindow(g_editor_hwnd, SW_SHOW);
    ShowWindow(editor_panel, SW_SHOW);
    resize_embedded_editor_to_parent();
    log_window_tree("after-embed-open", parent, g_editor_hwnd, editor_panel);
    keepsake_debug_log("bridge: editor embedded in host window %p\n",
                       static_cast<void *>(parent));
    return true;
}

bool gui_open_editor(BridgeLoader *loader, const EditorHeaderInfo &header) {
    return gui_call_sync([=]() { return gui_open_editor_impl(loader, header); });
}

bool gui_open_editor_embedded(BridgeLoader *loader, uint64_t native_handle) {
    return gui_call_sync([=]() { return gui_open_editor_embedded_impl(loader, native_handle); });
}

void gui_set_editor_transient(uint64_t native_handle) {
    gui_call_sync([=]() {
        g_owner_hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(native_handle));
        keepsake_debug_log("bridge: set transient owner=%p valid=%d\n",
                           static_cast<void *>(g_owner_hwnd),
                           IsWindow(g_owner_hwnd) ? 1 : 0);
        update_floating_owner();
    });
}

void gui_close_editor(BridgeLoader *loader) {
    gui_call_sync([=]() {
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
    g_editor_panel_hwnd = nullptr;
    g_active_loader = nullptr;
    g_parent_hwnd = nullptr;
    g_owner_hwnd = nullptr;
    g_last_parent_w = 0;
    g_last_parent_h = 0;
    g_editor_open = false;
    });
}

bool gui_get_editor_rect(BridgeLoader *loader, int &w, int &h) {
    return loader ? loader->get_editor_rect(w, h) : false;
}

void gui_idle(BridgeLoader *) {
    if (!g_editor_open) return;
    PostThreadMessageA(g_gui_thread_id, WM_NULL, 0, 0);
}

bool gui_is_open() { return g_editor_open; }
uint32_t gui_open_editor_iosurface(BridgeLoader *, int, int) { return 0; }
void gui_forward_mouse(const IpcMouseEvent &) {}
void gui_forward_key(const IpcKeyEvent &) {}
bool gui_is_iosurface_mode() { return false; }

#endif
