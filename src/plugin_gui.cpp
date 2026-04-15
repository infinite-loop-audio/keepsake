//
// Keepsake CLAP plugin — GUI extension.
//

#include "plugin_internal.h"
#include "debug_log.h"

#include <atomic>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
static const int GUI_OPEN_TIMEOUT_MS = 45000;
static const int GUI_EMBED_TIMEOUT_MS = 1500;
static const int GUI_PARENT_READY_TIMEOUT_MS = 750;
#else
static const int GUI_OPEN_TIMEOUT_MS = 5000;
#endif

static std::atomic<uint64_t> g_gui_lifecycle_seq{0};

static uint64_t gui_next_lifecycle_seq() {
    return g_gui_lifecycle_seq.fetch_add(1, std::memory_order_relaxed) + 1;
}

#ifdef _WIN32
static void gui_wait_for_win32_parent_ready(uint64_t handle, int timeout_ms) {
    HWND hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(handle));
    if (!hwnd || timeout_ms <= 0) {
        return;
    }

    auto deadline = GetTickCount64() + static_cast<ULONGLONG>(timeout_ms);
    while (GetTickCount64() < deadline) {
        LONG_PTR style = GetWindowLongPtrA(hwnd, GWL_STYLE);
        const bool visible = IsWindowVisible(hwnd) != FALSE;
        const bool style_visible = (style & WS_VISIBLE) != 0;
        if (visible || style_visible) return;
        Sleep(10);
    }
    keepsake_debug_log("keepsake: gui_wait_for_win32_parent_ready timeout hwnd=%p\n",
                       static_cast<void *>(hwnd));
}
#endif

static bool gui_refresh_size(KeepsakePlugin *kp) {
    if (!kp || kp->crashed || !kp->has_editor || !kp->bridge) return false;
    if (kp->editor_width > 0 && kp->editor_height > 0) return true;

    std::vector<uint8_t> payload;
    if (!send_and_wait(kp, IPC_OP_EDITOR_GET_RECT, nullptr, 0, &payload, 1000)) {
        keepsake_debug_log("keepsake: gui_refresh_size failed\n");
        return false;
    }
    if (payload.size() < sizeof(IpcEditorRect)) {
        keepsake_debug_log("keepsake: gui_refresh_size short payload=%zu\n",
                           payload.size());
        return false;
    }

    IpcEditorRect rect{};
    memcpy(&rect, payload.data(), sizeof(rect));
    if (rect.width <= 0 || rect.height <= 0) {
        keepsake_debug_log("keepsake: gui_refresh_size invalid=%dx%d\n",
                           rect.width, rect.height);
        return false;
    }

    kp->editor_width = rect.width;
    kp->editor_height = rect.height;
    keepsake_debug_log("keepsake: gui_refresh_size %dx%d\n",
                       kp->editor_width, kp->editor_height);
    return true;
}

static bool gui_open_floating(KeepsakePlugin *kp) {
    kp->gui_is_floating = true;
    kp->editor_open_pending = false;
    if (kp->shm.ptr) {
        shm_store_release(&shm_control(kp->shm.ptr)->editor_state, SHM_EDITOR_CLOSED);
    }
    if (kp->editor_open && !kp->crashed) {
        send_and_wait(kp, IPC_OP_EDITOR_CLOSE, nullptr, 0, nullptr, 1000);
        kp->editor_open = false;
    }
#ifdef _WIN32
    if (kp->gui_transient_handle != 0) {
        send_and_wait(kp, IPC_OP_EDITOR_SET_TRANSIENT,
                      &kp->gui_transient_handle,
                      sizeof(kp->gui_transient_handle),
                      nullptr, 1000);
    }
#endif
    if (!send_and_wait(kp, IPC_OP_EDITOR_OPEN, nullptr, 0, nullptr,
                       GUI_OPEN_TIMEOUT_MS)) {
        keepsake_debug_log("keepsake: floating fallback open failed\n");
        if (kp->bridge && platform_process_alive(kp->bridge->proc)) {
            abandon_bridge(kp, "EDITOR_OPEN failed while bridge still alive");
        }
        return false;
    }
    kp->editor_open = true;
    gui_refresh_size(kp);
    keepsake_debug_log("keepsake: floating fallback open OK\n");
    return true;
}

static bool gui_is_api_supported(const clap_plugin_t *plugin,
                                 const char *api, bool is_floating) {
    auto *kp = get(plugin);
    if (!kp->bridge_ok && !kp->crashed) wait_async_init(kp, 1500);
    if (!kp->has_editor) return false;
#ifdef __APPLE__
    (void)is_floating;
    return strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
#elif defined(_WIN32)
    (void)is_floating;
    return strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
#else
    (void)is_floating;
    return strcmp(api, CLAP_WINDOW_API_X11) == 0;
#endif
}

static bool gui_get_preferred_api(const clap_plugin_t *, const char **api,
                                  bool *is_floating) {
#ifdef __APPLE__
    *api = CLAP_WINDOW_API_COCOA;
    *is_floating = false;
#elif defined(_WIN32)
    *api = CLAP_WINDOW_API_WIN32;
    *is_floating = false;
#else
    *api = CLAP_WINDOW_API_X11;
    *is_floating = false;
#endif
    return true;
}

static bool gui_create(const clap_plugin_t *plugin, const char *, bool is_floating) {
    auto *kp = get(plugin);
    if (!kp->bridge_ok && !kp->crashed) wait_async_init(kp, 1500);
    if (!kp->has_editor || kp->crashed) return false;
#ifdef __APPLE__
    kp->gui_is_floating = true;
#elif defined(_WIN32)
    kp->gui_is_floating = is_floating || kp->gui_embed_failed;
#else
    kp->gui_is_floating = is_floating || kp->gui_embed_failed;
#endif
    keepsake_debug_log("keepsake: gui_create seq=%llu floating=%d has_editor=%d\n",
                       static_cast<unsigned long long>(gui_next_lifecycle_seq()),
                       kp->gui_is_floating ? 1 : 0, kp->has_editor ? 1 : 0);
    kp->gui_transient_handle = 0;
    kp->gui_scale = 1.0;
    return true;
}

#ifdef _WIN32
static void gui_suspend_processing_for_editor(KeepsakePlugin *kp) {
    if (!kp || kp->gui_processing_suspended || !kp->processing || kp->crashed) return;
    keepsake_debug_log("keepsake: gui_suspend_processing_for_editor instance=%u\n",
                       kp->instance_id);
    if (kp->bridge_ok) {
        if (!send_and_wait(kp, IPC_OP_STOP_PROC, nullptr, 0, nullptr, 250)) {
            keepsake_debug_log("keepsake: gui_suspend_processing_for_editor STOP_PROC failed\n");
            return;
        }
    }
    kp->processing = false;
    kp->gui_processing_suspended = true;
}

static void gui_resume_processing_after_editor(KeepsakePlugin *kp) {
    if (!kp || !kp->gui_processing_suspended || kp->crashed || !kp->active) return;
    keepsake_debug_log("keepsake: gui_resume_processing_after_editor instance=%u\n",
                       kp->instance_id);
    if (kp->bridge_ok) {
        if (!send_and_wait(kp, IPC_OP_START_PROC, nullptr, 0, nullptr, 250)) {
            keepsake_debug_log("keepsake: gui_resume_processing_after_editor START_PROC failed\n");
            if (kp->bridge && platform_process_alive(kp->bridge->proc)) {
                abandon_bridge(kp, "GUI resume START_PROC failed while bridge still alive");
            }
            return;
        }
    }
    kp->processing = true;
    kp->gui_processing_suspended = false;
}
#endif

static void gui_destroy(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    keepsake_debug_log("keepsake: gui_destroy seq=%llu open=%d pending=%d floating=%d\n",
                       static_cast<unsigned long long>(gui_next_lifecycle_seq()),
                       kp->editor_open ? 1 : 0,
                       kp->editor_open_pending ? 1 : 0,
                       kp->gui_is_floating ? 1 : 0);
    if (kp->editor_open_pending && kp->bridge && platform_process_alive(kp->bridge->proc)) {
        abandon_bridge(kp, "GUI destroy during pending editor open");
    }
    if (kp->editor_open && !kp->crashed) {
        send_and_wait(kp, IPC_OP_EDITOR_CLOSE);
        kp->editor_open = false;
    }
    kp->editor_open_pending = false;
    if (kp->shm.ptr) {
        shm_store_release(&shm_control(kp->shm.ptr)->editor_state, SHM_EDITOR_CLOSED);
    }
#ifdef _WIN32
    gui_resume_processing_after_editor(kp);
#endif
}

static bool gui_set_scale(const clap_plugin_t *plugin, double scale) {
    auto *kp = get(plugin);
    if (!std::isfinite(scale) || scale <= 0.0) scale = 1.0;
    kp->gui_scale = scale;
    keepsake_debug_log("keepsake: gui_set_scale seq=%llu scale=%.3f -> 1\n",
                       static_cast<unsigned long long>(gui_next_lifecycle_seq()),
                       kp->gui_scale);
    return true;
}

static bool gui_get_size(const clap_plugin_t *plugin, uint32_t *w, uint32_t *h) {
    auto *kp = get(plugin);
    if (!kp->bridge_ok && !kp->crashed) wait_async_init(kp, 1500);
    gui_refresh_size(kp);
    if (kp->editor_width > 0 && kp->editor_height > 0) {
#ifdef _WIN32
        if (kp->format == FORMAT_VST2) {
            *w = static_cast<uint32_t>(kp->editor_width);
            *h = static_cast<uint32_t>(kp->editor_height);
            keepsake_debug_log("keepsake: gui_get_size %ux%u raw=%dx%d scale=%.3f windows-vst2-unscaled=1\n",
                               *w, *h,
                               kp->editor_width, kp->editor_height,
                               kp->gui_scale);
            return true;
        }
#endif
        const double scale = (kp->gui_scale > 0.0) ? kp->gui_scale : 1.0;
        *w = static_cast<uint32_t>(std::lround(static_cast<double>(kp->editor_width) * scale));
        *h = static_cast<uint32_t>(std::lround(static_cast<double>(kp->editor_height) * scale));
        keepsake_debug_log("keepsake: gui_get_size %ux%u raw=%dx%d scale=%.3f\n",
                           *w, *h,
                           kp->editor_width, kp->editor_height,
                           scale);
        return true;
    }
    keepsake_debug_log("keepsake: gui_get_size unavailable\n");
    return false;
}

static bool gui_can_resize(const clap_plugin_t *) {
    keepsake_debug_log("keepsake: gui_can_resize seq=%llu -> 0\n",
                       static_cast<unsigned long long>(gui_next_lifecycle_seq()));
    return false;
}
static bool gui_get_resize_hints(const clap_plugin_t *, clap_gui_resize_hints_t *) {
    keepsake_debug_log("keepsake: gui_get_resize_hints seq=%llu -> 0\n",
                       static_cast<unsigned long long>(gui_next_lifecycle_seq()));
    return false;
}
static bool gui_adjust_size(const clap_plugin_t *, uint32_t *w, uint32_t *h) {
    keepsake_debug_log("keepsake: gui_adjust_size seq=%llu requested=%ux%u -> 0\n",
                       static_cast<unsigned long long>(gui_next_lifecycle_seq()),
                       w ? *w : 0, h ? *h : 0);
    return false;
}
static bool gui_set_size(const clap_plugin_t *, uint32_t w, uint32_t h) {
    keepsake_debug_log("keepsake: gui_set_size seq=%llu %ux%u -> 0\n",
                       static_cast<unsigned long long>(gui_next_lifecycle_seq()),
                       w, h);
    return false;
}

static bool gui_set_parent(const clap_plugin_t *plugin, const clap_window_t *window) {
    auto *kp = get(plugin);
    if (!kp->bridge_ok && !kp->crashed) wait_async_init(kp, 1500);
    if (kp->crashed || !kp->has_editor || !kp->bridge || !window) return false;

#ifdef __APPLE__
    if (!kp->editor_open) {
        if (!send_and_wait(kp, IPC_OP_EDITOR_OPEN, nullptr, 0, nullptr,
                           GUI_OPEN_TIMEOUT_MS)) {
            keepsake_debug_log("keepsake: gui_set_parent() editor open failed\n");
            return false;
        }
        kp->editor_open = true;
    }
    return true;
#elif defined(_WIN32)
    uint64_t handle = 0;
    handle = reinterpret_cast<uint64_t>(window->win32);
    kp->gui_transient_handle = handle;
    keepsake_debug_log("keepsake: gui_set_parent seq=%llu handle=%p floating=%d open=%d\n",
                       static_cast<unsigned long long>(gui_next_lifecycle_seq()),
                       reinterpret_cast<void *>(static_cast<uintptr_t>(handle)),
                       kp->gui_is_floating ? 1 : 0, kp->editor_open ? 1 : 0);
    kp->editor_open_pending = false;
    gui_wait_for_win32_parent_ready(handle, GUI_PARENT_READY_TIMEOUT_MS);

    if (kp->gui_embed_failed) {
        keepsake_debug_log("keepsake: gui_set_parent embed previously failed, keep floating fallback\n");
        return true;
    }

    if (!send_and_wait(kp, IPC_OP_EDITOR_SET_PARENT, &handle, sizeof(handle),
                       nullptr, GUI_EMBED_TIMEOUT_MS)) {
        if (platform_process_alive(kp->bridge->proc)) {
            keepsake_debug_log("keepsake: gui_set_parent embed timed out, abandoning live bridge\n");
            abandon_bridge(kp, "EDITOR_SET_PARENT failed while bridge still alive");
            return false;
        }
        keepsake_debug_log("keepsake: gui_set_parent embed hard failure\n");
        return false;
    }

    kp->gui_is_floating = false;
    keepsake_debug_log("keepsake: gui_set_parent embed staged\n");
    return true;
#else
    if (kp->gui_is_floating || kp->gui_embed_failed) {
        keepsake_debug_log("keepsake: gui_set_parent switching to floating path\n");
        return true;
    }
    uint64_t handle = 0;
    handle = static_cast<uint64_t>(window->x11);
    keepsake_debug_log("keepsake: gui_set_parent seq=%llu handle=%p\n",
                       static_cast<unsigned long long>(gui_next_lifecycle_seq()),
                       reinterpret_cast<void *>(static_cast<uintptr_t>(handle)));
    if (!send_and_wait(kp, IPC_OP_EDITOR_SET_PARENT, &handle, sizeof(handle),
                       nullptr, GUI_OPEN_TIMEOUT_MS)) {
#ifdef _WIN32
        if (platform_process_alive(kp->bridge->proc)) {
            keepsake_debug_log("keepsake: gui_set_parent() timed out, falling back to floating\n");
            kp->gui_embed_failed = true;
            return gui_open_floating(kp);
        }
#endif
        keepsake_debug_log("keepsake: gui_set_parent() editor parent failed\n");
#ifdef _WIN32
        kp->gui_embed_failed = true;
        return gui_open_floating(kp);
#else
        return false;
#endif
    }
    kp->editor_open = true;
    keepsake_debug_log("keepsake: gui_set_parent success floating=%d\n",
                       kp->gui_is_floating ? 1 : 0);
    return true;
#endif

    return false;
}

static bool gui_set_transient(const clap_plugin_t *plugin, const clap_window_t *window) {
    auto *kp = get(plugin);
    if (!kp->bridge_ok && !kp->crashed) wait_async_init(kp, 1500);
    if (kp->crashed || !kp->has_editor || !kp->bridge) return false;

#ifdef _WIN32
    uint64_t handle = 0;
    if (window) handle = reinterpret_cast<uint64_t>(window->win32);
    kp->gui_transient_handle = handle;
    keepsake_debug_log("keepsake: gui_set_transient seq=%llu handle=%p\n",
                       static_cast<unsigned long long>(gui_next_lifecycle_seq()),
                       reinterpret_cast<void *>(static_cast<uintptr_t>(handle)));
    if (handle == 0) return true;
    return send_and_wait(kp, IPC_OP_EDITOR_SET_TRANSIENT, &handle, sizeof(handle),
                         nullptr, 1000);
#else
    (void)window;
    return true;
#endif
}
static void gui_suggest_title(const clap_plugin_t *, const char *) {}

static bool gui_show(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    if (!kp->bridge_ok && !kp->crashed) wait_async_init(kp, 1500);
    if (kp->crashed || !kp->has_editor) return false;
    keepsake_debug_log("keepsake: gui_show seq=%llu floating=%d open=%d pending=%d\n",
                       static_cast<unsigned long long>(gui_next_lifecycle_seq()),
                       kp->gui_is_floating ? 1 : 0,
                       kp->editor_open ? 1 : 0,
                       kp->editor_open_pending ? 1 : 0);
    if (kp->gui_embed_failed && !kp->gui_is_floating) {
        keepsake_debug_log("keepsake: gui_show blocked after embed timeout on current bridge instance\n");
        return false;
    }
#ifdef _WIN32
    keepsake_debug_log("keepsake: gui_show leaving processing live during pending editor open\n");
#endif
    if (!kp->editor_open && !kp->editor_open_pending) {
        if (kp->shm.ptr) {
            shm_store_release(&shm_control(kp->shm.ptr)->editor_state,
                              kp->gui_is_floating ? SHM_EDITOR_CLOSED : SHM_EDITOR_OPENING);
        }
        if (!send_and_wait(kp, IPC_OP_EDITOR_OPEN, nullptr, 0, nullptr,
                           GUI_OPEN_TIMEOUT_MS)) {
            keepsake_debug_log("keepsake: gui_show() editor open failed\n");
            if (kp->bridge && platform_process_alive(kp->bridge->proc)) {
                abandon_bridge(kp, "GUI show editor open failed while bridge still alive");
            }
            return false;
        }
        if (kp->gui_is_floating) {
            kp->editor_open = true;
            kp->editor_open_pending = false;
            if (kp->shm.ptr) {
                shm_store_release(&shm_control(kp->shm.ptr)->editor_state, SHM_EDITOR_OPEN);
            }
        } else {
            kp->editor_open = false;
            kp->editor_open_pending = true;
            if (kp->host && kp->host->request_callback) {
                kp->host->request_callback(kp->host);
            }
        }
    }
    keepsake_debug_log("keepsake: gui_show success seq=%llu floating=%d open=%d pending=%d\n",
                       static_cast<unsigned long long>(gui_next_lifecycle_seq()),
                       kp->gui_is_floating ? 1 : 0,
                       kp->editor_open ? 1 : 0,
                       kp->editor_open_pending ? 1 : 0);
    return true;
}

void gui_complete_pending_open(KeepsakePlugin *kp) {
    if (!kp) return;
    kp->editor_open_pending = false;
    kp->editor_open = true;
    if (kp->shm.ptr) {
        shm_store_release(&shm_control(kp->shm.ptr)->editor_state, SHM_EDITOR_OPEN);
    }
#ifdef _WIN32
    gui_resume_processing_after_editor(kp);
#endif
}

static bool gui_hide(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    keepsake_debug_log("keepsake: gui_hide seq=%llu open=%d pending=%d\n",
                       static_cast<unsigned long long>(gui_next_lifecycle_seq()),
                       kp->editor_open ? 1 : 0,
                       kp->editor_open_pending ? 1 : 0);
    if (kp->editor_open_pending && kp->bridge && platform_process_alive(kp->bridge->proc)) {
        abandon_bridge(kp, "GUI hide during pending editor open");
    }
    if (kp->editor_open && !kp->crashed) {
        send_and_wait(kp, IPC_OP_EDITOR_CLOSE);
        kp->editor_open = false;
    }
    kp->editor_open_pending = false;
    if (kp->shm.ptr) {
        shm_store_release(&shm_control(kp->shm.ptr)->editor_state, SHM_EDITOR_CLOSED);
    }
#ifdef _WIN32
    gui_resume_processing_after_editor(kp);
#endif
    return true;
}

extern const clap_plugin_gui_t keepsake_gui_ext;
const clap_plugin_gui_t keepsake_gui_ext = {
    .is_api_supported = gui_is_api_supported,
    .get_preferred_api = gui_get_preferred_api,
    .create = gui_create,
    .destroy = gui_destroy,
    .set_scale = gui_set_scale,
    .get_size = gui_get_size,
    .can_resize = gui_can_resize,
    .get_resize_hints = gui_get_resize_hints,
    .adjust_size = gui_adjust_size,
    .set_size = gui_set_size,
    .set_parent = gui_set_parent,
    .set_transient = gui_set_transient,
    .suggest_title = gui_suggest_title,
    .show = gui_show,
    .hide = gui_hide,
};
