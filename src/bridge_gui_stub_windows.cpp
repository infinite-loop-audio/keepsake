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
static uint64_t g_staged_embed_parent_handle = 0;
static BridgeLoader *g_pending_open_loader = nullptr;
static std::atomic<bool> g_pending_open{false};
static bool g_pending_open_embedded = false;
static EditorHeaderInfo g_pending_open_header;
static std::atomic<bool> g_editor_open_inflight{false};
static ShmProcessControl *g_gui_status_ctrl = nullptr;
static UINT_PTR g_idle_timer = 0;
static EditorHeaderInfo g_header_info;
static const int WIN_HEADER_HEIGHT = 24;
static int g_last_parent_w = 0;
static int g_last_parent_h = 0;
static bool g_editor_hwnd_on_window_thread = false;
static bool g_editor_embedded_surface_ready = false;
static std::atomic<bool> g_editor_open{false};
static constexpr const char *kKeepsakeHostWindowClass = "KeepsakeHostWindow";
static constexpr const char *kKeepsakeEmbedWrapperClass = "KeepsakeEmbedWrapper";
static constexpr const char *kKeepsakeEmbedPanelClass = "KeepsakeEmbedPanel";
static constexpr DWORD kKeepsakeEmbedChildStyle =
    WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
static constexpr DWORD kKeepsakeEmbedWrapperExStyle = WS_EX_CONTROLPARENT;
static constexpr UINT WM_KEEPSAKE_GUI_TASK = WM_APP + 41;
static constexpr UINT WM_KEEPSAKE_WINDOW_TASK = WM_APP + 42;

enum class EmbedOpenMode {
    MainThread,
    Hybrid,
    WindowThread,
};

enum class EmbedAttachTarget {
    DirectParent,
    RootAncestor,
};

enum class EmbedParentSurface {
    Panel,
    Wrapper,
};

static DWORD g_gui_thread_id = 0;
static std::mutex g_gui_mutex;
static bool g_gui_ready = false;
static std::queue<std::function<void()>> g_gui_tasks;
static std::thread g_window_thread;
static DWORD g_window_thread_id = 0;
static std::mutex g_window_mutex;
static std::condition_variable g_window_ready_cv;
static bool g_window_ready = false;
static std::queue<std::function<void()>> g_window_tasks;

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
    HWND root = GetAncestor(hwnd, GA_ROOT);
    HWND foreground = GetForegroundWindow();
    HWND focus = GetFocus();
    DWORD window_pid = 0;
    DWORD window_tid = GetWindowThreadProcessId(hwnd, &window_pid);
    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    bool have_placement = GetWindowPlacement(hwnd, &placement) != FALSE;
    keepsake_debug_log(
        "bridge: %s hwnd=%p class='%s' style=0x%llx exstyle=0x%llx parent=%p owner=%p root=%p visible=%d enabled=%d iconic=%d zoomed=%d fg=%d focus=%d tid=%lu pid=%lu show=%u rect=%ld,%ld,%ld,%ld\n",
        label,
        static_cast<void *>(hwnd),
        klass,
        static_cast<long long>(style),
        static_cast<long long>(exstyle),
        static_cast<void *>(parent),
        static_cast<void *>(owner),
        static_cast<void *>(root),
        IsWindowVisible(hwnd) ? 1 : 0,
        IsWindowEnabled(hwnd) ? 1 : 0,
        IsIconic(hwnd) ? 1 : 0,
        IsZoomed(hwnd) ? 1 : 0,
        foreground == hwnd ? 1 : 0,
        focus == hwnd ? 1 : 0,
        static_cast<unsigned long>(window_tid),
        static_cast<unsigned long>(window_pid),
        have_placement ? static_cast<unsigned>(placement.showCmd) : 0U,
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

static void window_drain_tasks() {
    std::queue<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(g_window_mutex);
        tasks.swap(g_window_tasks);
    }
    while (!tasks.empty()) {
        auto task = std::move(tasks.front());
        tasks.pop();
        task();
    }
}

static const char *embed_open_mode_name(EmbedOpenMode mode) {
    switch (mode) {
    case EmbedOpenMode::MainThread:
        return "main";
    case EmbedOpenMode::Hybrid:
        return "hybrid";
    case EmbedOpenMode::WindowThread:
        return "window";
    }
    return "hybrid";
}

static EmbedOpenMode get_embed_open_mode() {
    static std::once_flag once;
    static EmbedOpenMode mode = EmbedOpenMode::MainThread;
    std::call_once(once, []() {
        char value[64] = {};
        DWORD len = GetEnvironmentVariableA("KEEPSAKE_WIN_EMBED_MODE",
                                            value,
                                            static_cast<DWORD>(sizeof(value)));
        if (len > 0 && len < sizeof(value)) {
            if (_stricmp(value, "main") == 0) {
                mode = EmbedOpenMode::MainThread;
            } else if (_stricmp(value, "window") == 0 ||
                       _stricmp(value, "legacy") == 0 ||
                       _stricmp(value, "all-helper") == 0) {
                mode = EmbedOpenMode::WindowThread;
            }
        }
        keepsake_debug_log("bridge: embed open mode=%s env='%s'\n",
                           embed_open_mode_name(mode),
                           len > 0 ? value : "");
    });
    return mode;
}

static const char *embed_attach_target_name(EmbedAttachTarget target) {
    switch (target) {
    case EmbedAttachTarget::DirectParent:
        return "direct";
    case EmbedAttachTarget::RootAncestor:
        return "root";
    }
    return "direct";
}

static EmbedAttachTarget get_embed_attach_target() {
    static std::once_flag once;
    static EmbedAttachTarget target = EmbedAttachTarget::DirectParent;
    std::call_once(once, []() {
        char value[64] = {};
        DWORD len = GetEnvironmentVariableA("KEEPSAKE_WIN_EMBED_ATTACH_TARGET",
                                            value,
                                            static_cast<DWORD>(sizeof(value)));
        if (len > 0 && len < sizeof(value)) {
            if (_stricmp(value, "root") == 0 ||
                _stricmp(value, "ancestor") == 0) {
                target = EmbedAttachTarget::RootAncestor;
            }
        }
        keepsake_debug_log("bridge: embed attach target=%s env='%s'\n",
                           embed_attach_target_name(target),
                           len > 0 ? value : "");
    });
    return target;
}

static HWND resolve_embed_attach_parent(HWND parent) {
    HWND target = parent;
    if (get_embed_attach_target() == EmbedAttachTarget::RootAncestor) {
        HWND root = GetAncestor(parent, GA_ROOT);
        if (root) target = root;
    }
    keepsake_debug_log("bridge: resolve_embed_attach_parent input=%p target=%p mode=%s\n",
                       static_cast<void *>(parent),
                       static_cast<void *>(target),
                       embed_attach_target_name(get_embed_attach_target()));
    return target;
}

static bool should_force_show_embed_parent() {
    static std::once_flag once;
    static bool enabled = false;
    std::call_once(once, []() {
        char value[16] = {};
        DWORD len = GetEnvironmentVariableA("KEEPSAKE_WIN_FORCE_SHOW_PARENT",
                                            value,
                                            static_cast<DWORD>(sizeof(value)));
        enabled = (len > 0 && value[0] != '\0' && value[0] != '0');
        keepsake_debug_log("bridge: force show embed parent=%d env='%s'\n",
                           enabled ? 1 : 0,
                           len > 0 ? value : "");
    });
    return enabled;
}

static const char *embed_parent_surface_name(EmbedParentSurface surface) {
    switch (surface) {
    case EmbedParentSurface::Panel:
        return "panel";
    case EmbedParentSurface::Wrapper:
        return "wrapper";
    }
    return "panel";
}

static EmbedParentSurface get_embed_parent_surface() {
    static std::once_flag once;
    static EmbedParentSurface surface = EmbedParentSurface::Panel;
    std::call_once(once, []() {
        char value[64] = {};
        DWORD len = GetEnvironmentVariableA("KEEPSAKE_WIN_EMBED_PARENT_SURFACE",
                                            value,
                                            static_cast<DWORD>(sizeof(value)));
        if (len > 0 && len < sizeof(value)) {
            if (_stricmp(value, "wrapper") == 0) {
                surface = EmbedParentSurface::Wrapper;
            } else if (_stricmp(value, "panel") == 0) {
                surface = EmbedParentSurface::Panel;
            }
        }
        keepsake_debug_log("bridge: embed parent surface=%s env='%s'\n",
                           embed_parent_surface_name(surface),
                           len > 0 ? value : "");
    });
    return surface;
}

static void reset_embedded_surface_state() {
    g_editor_embedded_surface_ready = false;
}

template <typename Fn>
static auto gui_call_sync(Fn &&fn) -> decltype(fn()) {
    using Result = decltype(fn());
    if (GetCurrentThreadId() == g_gui_thread_id) return fn();

    auto promise = std::make_shared<std::promise<Result>>();
    auto future = promise->get_future();
    keepsake_debug_log("bridge: gui_call_sync enqueue gui_thread=%lu caller=%lu\n",
                       static_cast<unsigned long>(g_gui_thread_id),
                       static_cast<unsigned long>(GetCurrentThreadId()));
    {
        std::lock_guard<std::mutex> lock(g_gui_mutex);
        g_gui_tasks.push([promise, fn = std::forward<Fn>(fn)]() mutable {
            try {
                keepsake_debug_log("bridge: gui_call_sync task-begin thread=%lu\n",
                                   static_cast<unsigned long>(GetCurrentThreadId()));
                if constexpr (std::is_void_v<Result>) {
                    fn();
                    promise->set_value();
                } else {
                    promise->set_value(fn());
                }
                keepsake_debug_log("bridge: gui_call_sync task-end thread=%lu\n",
                                   static_cast<unsigned long>(GetCurrentThreadId()));
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
    }
    keepsake_debug_log("bridge: gui_call_sync post thread=%lu\n",
                       static_cast<unsigned long>(g_gui_thread_id));
    PostThreadMessageA(g_gui_thread_id, WM_KEEPSAKE_GUI_TASK, 0, 0);
    keepsake_debug_log("bridge: gui_call_sync wait-begin caller=%lu\n",
                       static_cast<unsigned long>(GetCurrentThreadId()));
    return future.get();
}

template <typename Fn>
static auto window_call_sync(Fn &&fn) -> decltype(fn()) {
    using Result = decltype(fn());
    if (GetCurrentThreadId() == g_window_thread_id) return fn();

    auto promise = std::make_shared<std::promise<Result>>();
    auto future = promise->get_future();
    keepsake_debug_log("bridge: window_call_sync enqueue window_thread=%lu caller=%lu\n",
                       static_cast<unsigned long>(g_window_thread_id),
                       static_cast<unsigned long>(GetCurrentThreadId()));
    {
        std::lock_guard<std::mutex> lock(g_window_mutex);
        g_window_tasks.push([promise, fn = std::forward<Fn>(fn)]() mutable {
            try {
                keepsake_debug_log("bridge: window_call_sync task-begin thread=%lu\n",
                                   static_cast<unsigned long>(GetCurrentThreadId()));
                if constexpr (std::is_void_v<Result>) {
                    fn();
                    promise->set_value();
                } else {
                    promise->set_value(fn());
                }
                keepsake_debug_log("bridge: window_call_sync task-end thread=%lu\n",
                                   static_cast<unsigned long>(GetCurrentThreadId()));
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
    }
    PostThreadMessageA(g_window_thread_id, WM_KEEPSAKE_WINDOW_TASK, 0, 0);
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

static bool ensure_embedded_surface(EmbedOpenMode mode, int w, int h) {
    if (g_editor_hwnd && g_editor_panel_hwnd && g_editor_embedded_surface_ready)
        return true;

    const bool use_window_thread_for_host_window = (mode != EmbedOpenMode::MainThread);
    auto create_surface = [=]() -> bool {
        const DWORD wrapper_style = WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        keepsake_debug_log("bridge: ensure_embedded_surface create mode=%s size=%dx%d thread=%lu\n",
                           embed_open_mode_name(mode),
                           w,
                           h,
                           static_cast<unsigned long>(GetCurrentThreadId()));

        HWND wrapper = CreateWindowExA(
            kKeepsakeEmbedWrapperExStyle, kKeepsakeEmbedWrapperClass, nullptr,
            wrapper_style,
            0, 0, w, h,
            nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
        DWORD wrapper_err = GetLastError();
        keepsake_debug_log("bridge: ensure_embedded_surface wrapper hwnd=%p err=%lu\n",
                           static_cast<void *>(wrapper),
                           static_cast<unsigned long>(wrapper_err));
        if (!wrapper) return false;

        HWND panel = CreateWindowExA(
            0, "STATIC", nullptr,
            kKeepsakeEmbedChildStyle | WS_VISIBLE,
            0, 0, w, h,
            wrapper, nullptr, GetModuleHandle(nullptr), nullptr);
        DWORD panel_err = GetLastError();
        keepsake_debug_log("bridge: ensure_embedded_surface panel hwnd=%p err=%lu\n",
                           static_cast<void *>(panel),
                           static_cast<unsigned long>(panel_err));
        if (!panel) {
            DestroyWindow(wrapper);
            return false;
        }

        g_editor_hwnd = wrapper;
        g_editor_panel_hwnd = panel;
        g_editor_hwnd_on_window_thread = use_window_thread_for_host_window;
        g_editor_embedded_surface_ready = true;
        return true;
    };

    return use_window_thread_for_host_window ? window_call_sync(create_surface)
                                             : create_surface();
}

static bool attach_embedded_surface(HWND parent, EmbedOpenMode mode, int w, int h) {
    if (!g_editor_hwnd || !g_editor_panel_hwnd) return false;

    const bool use_window_thread_for_host_window = (mode != EmbedOpenMode::MainThread);
    auto attach_surface = [=]() -> bool {
        keepsake_debug_log("bridge: attach_embedded_surface wrapper=%p panel=%p parent=%p mode=%s thread=%lu\n",
                           static_cast<void *>(g_editor_hwnd),
                           static_cast<void *>(g_editor_panel_hwnd),
                           static_cast<void *>(parent),
                           embed_open_mode_name(mode),
                           static_cast<unsigned long>(GetCurrentThreadId()));

        LONG_PTR style = GetWindowLongPtrA(g_editor_hwnd, GWL_STYLE);
        style &= ~static_cast<LONG_PTR>(WS_POPUP);
        style |= kKeepsakeEmbedChildStyle;
        SetWindowLongPtrA(g_editor_hwnd, GWL_STYLE, style);

        SetLastError(0);
        HWND previous_parent = SetParent(g_editor_hwnd, parent);
        DWORD parent_err = GetLastError();
        keepsake_debug_log("bridge: attach_embedded_surface SetParent previous=%p err=%lu\n",
                           static_cast<void *>(previous_parent),
                           static_cast<unsigned long>(parent_err));
        if (!previous_parent && parent_err != 0) return false;

        MoveWindow(g_editor_hwnd, 0, 0, w, h, TRUE);
        MoveWindow(g_editor_panel_hwnd, 0, 0, w, h, TRUE);
        ShowWindow(g_editor_hwnd, SW_SHOW);
        ShowWindow(g_editor_panel_hwnd, SW_SHOW);
        UpdateWindow(g_editor_hwnd);
        UpdateWindow(g_editor_panel_hwnd);
        keepsake_debug_log(
            "bridge: attach_embedded_surface visible wrapper=%d panel=%d parent=%d\n",
            IsWindowVisible(g_editor_hwnd) ? 1 : 0,
            IsWindowVisible(g_editor_panel_hwnd) ? 1 : 0,
            IsWindowVisible(parent) ? 1 : 0);
        return true;
    };

    return use_window_thread_for_host_window ? window_call_sync(attach_surface)
                                             : attach_surface();
}

static void wait_for_embed_parent_visibility(HWND parent, int timeout_ms) {
    if (!parent || timeout_ms <= 0) return;

    auto deadline = GetTickCount64() + static_cast<ULONGLONG>(timeout_ms);
    while (GetTickCount64() < deadline) {
        HWND root = GetAncestor(parent, GA_ROOT);
        const bool parent_visible = IsWindowVisible(parent) != FALSE;
        const bool root_visible = root ? (IsWindowVisible(root) != FALSE) : false;
        if (parent_visible || root_visible) return;

        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        Sleep(10);
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
        reset_embedded_surface_state();
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
    if (g_gui_ready) return;
    g_gui_thread_id = GetCurrentThreadId();
    keepsake_debug_log("bridge: gui owner thread init id=%lu\n",
                       static_cast<unsigned long>(g_gui_thread_id));
    MSG msg;
    PeekMessageA(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    gui_register_classes();
    g_gui_ready = true;
    keepsake_debug_log("bridge: gui owner thread ready id=%lu\n",
                       static_cast<unsigned long>(g_gui_thread_id));

    {
        std::unique_lock<std::mutex> lock(g_window_mutex);
        if (!g_window_ready) {
            g_window_thread = std::thread([]() {
                g_window_thread_id = GetCurrentThreadId();
                keepsake_debug_log("bridge: helper window thread init id=%lu\n",
                                   static_cast<unsigned long>(g_window_thread_id));
                MSG msg;
                PeekMessageA(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
                {
                    std::lock_guard<std::mutex> ready_lock(g_window_mutex);
                    g_window_ready = true;
                }
                keepsake_debug_log("bridge: helper window thread ready id=%lu\n",
                                   static_cast<unsigned long>(g_window_thread_id));
                g_window_ready_cv.notify_all();

                while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
                    if (msg.message == WM_KEEPSAKE_WINDOW_TASK) {
                        window_drain_tasks();
                        continue;
                    }
                    TranslateMessage(&msg);
                    DispatchMessageA(&msg);
                    window_drain_tasks();
                }
            });
            g_window_thread.detach();
            g_window_ready_cv.wait(lock, [] { return g_window_ready; });
        }
    }
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

static bool gui_open_editor_embedded_impl(BridgeLoader *loader,
                                          uint64_t native_handle,
                                          EmbedOpenMode mode) {
    if (!loader || !loader->has_editor()) return false;
    if (g_editor_open) return true;
    keepsake_debug_log("bridge: gui_open_editor_embedded_impl enter handle=%p thread=%lu mode=%s\n",
                       reinterpret_cast<void *>(static_cast<uintptr_t>(native_handle)),
                       static_cast<unsigned long>(GetCurrentThreadId()),
                       embed_open_mode_name(mode));

    HWND parent = reinterpret_cast<HWND>(static_cast<uintptr_t>(native_handle));
    keepsake_debug_log("bridge: gui_open_editor_embedded_impl before IsWindow parent=%p\n",
                       static_cast<void *>(parent));
    BOOL parent_ok = IsWindow(parent);
    keepsake_debug_log("bridge: gui_open_editor_embedded_impl after IsWindow parent=%p ok=%d\n",
                       static_cast<void *>(parent),
                       parent_ok ? 1 : 0);
    if (!parent_ok) return false;

    int w = 640, h = 480;
    keepsake_debug_log("bridge: gui_open_editor_embedded_impl before get_editor_rect default=%dx%d\n", w, h);
    loader->get_editor_rect(w, h);
    keepsake_debug_log("bridge: gui_open_editor_embedded_impl after get_editor_rect size=%dx%d\n", w, h);

    const bool use_window_thread_for_host_window = (mode != EmbedOpenMode::MainThread);
    const bool use_window_thread_for_plugin_open = (mode == EmbedOpenMode::WindowThread);
    HWND attach_parent = resolve_embed_attach_parent(parent);

    keepsake_debug_log("bridge: gui_open_editor_embedded_impl before ensure_embedded_surface parent=%p size=%dx%d via=%s\n",
                       static_cast<void *>(attach_parent),
                       w,
                       h,
                       use_window_thread_for_host_window ? "window" : "main");
    if (!ensure_embedded_surface(mode, w, h)) {
        keepsake_debug_log("bridge: ensure_embedded_surface failed, reusing host parent directly\n");
    } else if (!attach_embedded_surface(attach_parent, mode, w, h)) {
        keepsake_debug_log("bridge: attach_embedded_surface failed\n");
    }
    keepsake_debug_log("bridge: gui_open_editor_embedded_impl after ensure/attach wrapper=%p panel=%p ready=%d\n",
                       static_cast<void *>(g_editor_hwnd),
                       static_cast<void *>(g_editor_panel_hwnd),
                       g_editor_embedded_surface_ready ? 1 : 0);
    HWND editor_parent = parent;
    switch (get_embed_parent_surface()) {
    case EmbedParentSurface::Panel:
        editor_parent = g_editor_panel_hwnd ? g_editor_panel_hwnd
                                            : (g_editor_hwnd ? g_editor_hwnd : parent);
        break;
    case EmbedParentSurface::Wrapper:
        editor_parent = g_editor_hwnd ? g_editor_hwnd : parent;
        break;
    }
    keepsake_debug_log("bridge: selected editor_parent=%p surface=%s\n",
                       static_cast<void *>(editor_parent),
                       embed_parent_surface_name(get_embed_parent_surface()));
    wait_for_embed_parent_visibility(attach_parent, 500);
    if (should_force_show_embed_parent() && !IsWindowVisible(attach_parent)) {
        keepsake_debug_log("bridge: force showing embed parent=%p\n",
                           static_cast<void *>(attach_parent));
        ShowWindow(attach_parent, SW_SHOWNA);
        UpdateWindow(attach_parent);
    }
    keepsake_debug_log(
        "bridge: pre-open visibility attach_parent=%p parent_visible=%d editor_parent=%p editor_visible=%d wrapper=%p wrapper_visible=%d panel=%p panel_visible=%d\n",
        static_cast<void *>(attach_parent),
        (attach_parent && IsWindowVisible(attach_parent)) ? 1 : 0,
        static_cast<void *>(editor_parent),
        (editor_parent && IsWindowVisible(editor_parent)) ? 1 : 0,
        static_cast<void *>(g_editor_hwnd),
        (g_editor_hwnd && IsWindowVisible(g_editor_hwnd)) ? 1 : 0,
        static_cast<void *>(g_editor_panel_hwnd),
        (g_editor_panel_hwnd && IsWindowVisible(g_editor_panel_hwnd)) ? 1 : 0);
    g_active_loader = loader;
    g_parent_hwnd = attach_parent;
    g_last_parent_w = 0;
    g_last_parent_h = 0;
    if (g_editor_hwnd) {
        auto start_timer = [=]() { return SetTimer(g_editor_hwnd, 1, 16, nullptr); };
        g_idle_timer = use_window_thread_for_host_window
            ? window_call_sync(start_timer)
            : start_timer();
    } else {
        g_idle_timer = 0;
    }
    log_window_tree("before-embed-open", attach_parent, g_editor_hwnd, g_editor_panel_hwnd);

    keepsake_debug_log("bridge: embedded editor open begin parent=%p thread=%lu\n",
                       static_cast<void *>(editor_parent),
                       static_cast<unsigned long>(GetCurrentThreadId()));
    auto open_editor = [=]() { return loader->open_editor(static_cast<void *>(editor_parent)); };
    bool open_ok = use_window_thread_for_plugin_open
        ? window_call_sync(open_editor)
        : open_editor();
    keepsake_debug_log("bridge: embedded editor open end ok=%d thread=%lu\n",
                       open_ok ? 1 : 0,
                       static_cast<unsigned long>(GetCurrentThreadId()));

    if (!open_ok) {
        keepsake_debug_log("bridge: embedded editor open failed\n");
        if (g_idle_timer) {
            if (g_editor_hwnd_on_window_thread) {
                window_call_sync([=]() { KillTimer(g_editor_hwnd, g_idle_timer); });
            } else {
                KillTimer(g_editor_hwnd, g_idle_timer);
            }
            g_idle_timer = 0;
        }
        if (g_editor_hwnd) {
            if (g_editor_hwnd_on_window_thread) {
                window_call_sync([=]() { DestroyWindow(g_editor_hwnd); });
            } else {
                DestroyWindow(g_editor_hwnd);
            }
        }
        g_editor_panel_hwnd = nullptr;
        g_editor_hwnd = nullptr;
        g_editor_hwnd_on_window_thread = false;
        reset_embedded_surface_state();
        g_active_loader = nullptr;
        g_parent_hwnd = nullptr;
        return false;
    }

    g_editor_open = true;

    resize_embedded_editor_to_parent();
    if (g_editor_hwnd) {
        if (use_window_thread_for_host_window) {
            window_call_sync([=]() { ShowWindow(g_editor_hwnd, SW_SHOW); });
        } else {
            ShowWindow(g_editor_hwnd, SW_SHOW);
        }
    }
    resize_embedded_editor_to_parent();
    log_window_tree("after-embed-open", attach_parent, g_editor_hwnd, g_editor_panel_hwnd);
    keepsake_debug_log("bridge: editor embedded in host window %p\n",
                       static_cast<void *>(attach_parent));
    return true;
}

bool gui_open_editor(BridgeLoader *loader, const EditorHeaderInfo &header) {
    if (!loader || !loader->has_editor()) return false;
    if (g_editor_open) return true;

    if (g_staged_embed_parent_handle != 0) {
        g_pending_open_loader = loader;
        g_pending_open_header = header;
        g_pending_open_embedded = true;
        g_pending_open.store(true);
        keepsake_debug_log("bridge: gui_open_editor queued async embed open parent=%p\n",
                           reinterpret_cast<void *>(static_cast<uintptr_t>(g_staged_embed_parent_handle)));
        return true;
    }

    keepsake_debug_log("bridge: gui_open_editor synchronous floating open\n");
    return gui_open_editor_impl(loader, header);
}

bool gui_open_editor_embedded(BridgeLoader *loader, uint64_t native_handle) {
    EmbedOpenMode mode = get_embed_open_mode();
    if (mode == EmbedOpenMode::WindowThread) {
        return window_call_sync([=]() {
            return gui_open_editor_embedded_impl(loader, native_handle, mode);
        });
    }
    return gui_call_sync([=]() {
        return gui_open_editor_embedded_impl(loader, native_handle, mode);
    });
}

bool gui_stage_editor_parent(BridgeLoader *loader, uint64_t native_handle) {
    if (!loader || !loader->has_editor()) return false;
    g_staged_embed_parent_handle = native_handle;
    g_pending_open_loader = loader;
    keepsake_debug_log("bridge: staged embed parent=%p\n",
                       reinterpret_cast<void *>(static_cast<uintptr_t>(native_handle)));
    return true;
}

bool gui_has_pending_work() {
    return g_pending_open.load();
}

void gui_get_editor_status(bool &open, bool &pending) {
    open = g_editor_open.load();
    pending = g_pending_open.load() || g_editor_open_inflight.load();
}

void gui_set_status_shm(void *shm_ptr) {
    g_gui_status_ctrl = shm_ptr ? shm_control(shm_ptr) : nullptr;
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
    auto close_impl = [=]() {
    if (!g_editor_open) {
        g_staged_embed_parent_handle = 0;
        g_pending_open_loader = nullptr;
        g_pending_open.store(false);
        g_pending_open_embedded = false;
        if (g_gui_status_ctrl) {
            shm_store_release(&g_gui_status_ctrl->editor_state, SHM_EDITOR_CLOSED);
        }
        return;
    }
    if (g_idle_timer) {
        if (g_editor_hwnd_on_window_thread) {
            window_call_sync([=]() { KillTimer(g_editor_hwnd, g_idle_timer); });
        } else {
            KillTimer(g_editor_hwnd, g_idle_timer);
        }
        g_idle_timer = 0;
    }
    if (loader) loader->close_editor();
    else if (g_active_loader) g_active_loader->close_editor();
    if (g_editor_hwnd) {
        if (g_editor_hwnd_on_window_thread) {
            window_call_sync([=]() { DestroyWindow(g_editor_hwnd); });
        } else {
            DestroyWindow(g_editor_hwnd);
        }
        g_editor_hwnd = nullptr;
    }
    g_editor_panel_hwnd = nullptr;
    g_active_loader = nullptr;
    g_parent_hwnd = nullptr;
    g_owner_hwnd = nullptr;
    g_staged_embed_parent_handle = 0;
    g_pending_open_loader = nullptr;
    g_pending_open.store(false);
    g_pending_open_embedded = false;
    g_editor_hwnd_on_window_thread = false;
    reset_embedded_surface_state();
    g_last_parent_w = 0;
    g_last_parent_h = 0;
    g_editor_open = false;
    if (g_gui_status_ctrl) {
        shm_store_release(&g_gui_status_ctrl->editor_state, SHM_EDITOR_CLOSED);
    }
    };
    if (get_embed_open_mode() == EmbedOpenMode::WindowThread) {
        window_call_sync(close_impl);
        return;
    }
    gui_call_sync(close_impl);
}

bool gui_get_editor_rect(BridgeLoader *loader, int &w, int &h) {
    return loader ? loader->get_editor_rect(w, h) : false;
}

void gui_idle(BridgeLoader *) {
    if (!g_editor_open && g_pending_open.load() && g_pending_open_loader) {
        bool ok = false;
        if (g_pending_open_embedded && g_staged_embed_parent_handle != 0) {
            g_editor_open_inflight.store(true);
            if (g_gui_status_ctrl) {
                shm_store_release(&g_gui_status_ctrl->editor_state, SHM_EDITOR_OPENING);
            }
            keepsake_debug_log("bridge: gui_idle opening staged embed parent=%p\n",
                               reinterpret_cast<void *>(static_cast<uintptr_t>(g_staged_embed_parent_handle)));
            ok = gui_open_editor_embedded_impl(g_pending_open_loader,
                                               g_staged_embed_parent_handle,
                                               get_embed_open_mode());
            keepsake_debug_log("bridge: gui_idle staged embed result=%d\n", ok ? 1 : 0);
            if (g_gui_status_ctrl) {
                shm_store_release(&g_gui_status_ctrl->editor_state,
                                  ok ? SHM_EDITOR_OPEN : SHM_EDITOR_FAILED);
            }
            g_editor_open_inflight.store(false);
        } else {
            keepsake_debug_log("bridge: gui_idle opening async floating editor\n");
            ok = gui_open_editor_impl(g_pending_open_loader, g_pending_open_header);
            keepsake_debug_log("bridge: gui_idle async floating result=%d\n", ok ? 1 : 0);
        }
        g_pending_open.store(false);
        if (!ok) {
            g_pending_open_loader = nullptr;
            g_staged_embed_parent_handle = 0;
        }
    }
    if (!g_editor_open) return;
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_KEEPSAKE_GUI_TASK) {
            gui_drain_tasks();
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    gui_drain_tasks();
}

bool gui_is_open() { return g_editor_open; }
uint32_t gui_open_editor_iosurface(BridgeLoader *, int, int) { return 0; }
void gui_forward_mouse(const IpcMouseEvent &) {}
void gui_forward_key(const IpcKeyEvent &) {}
bool gui_is_iosurface_mode() { return false; }

#endif
