//
// Keepsake CLAP plugin — GUI extension.
//

#include "plugin_internal.h"
#include "debug_log.h"

#ifdef _WIN32
static const int GUI_OPEN_TIMEOUT_MS = 45000;
#else
static const int GUI_OPEN_TIMEOUT_MS = 5000;
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
    kp->gui_is_floating = true;
#else
    kp->gui_is_floating = is_floating || kp->gui_embed_failed;
#endif
    keepsake_debug_log("keepsake: gui_create floating=%d has_editor=%d\n",
                       kp->gui_is_floating ? 1 : 0, kp->has_editor ? 1 : 0);
    kp->gui_transient_handle = 0;
    return true;
}

static void gui_destroy(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    keepsake_debug_log("keepsake: gui_destroy open=%d floating=%d\n",
                       kp->editor_open ? 1 : 0, kp->gui_is_floating ? 1 : 0);
    if (kp->editor_open && !kp->crashed) {
        send_and_wait(kp, IPC_OP_EDITOR_CLOSE);
        kp->editor_open = false;
    }
}

static bool gui_set_scale(const clap_plugin_t *, double) { return false; }

static bool gui_get_size(const clap_plugin_t *plugin, uint32_t *w, uint32_t *h) {
    auto *kp = get(plugin);
    if (!kp->bridge_ok && !kp->crashed) wait_async_init(kp, 1500);
    gui_refresh_size(kp);
    if (kp->editor_width > 0 && kp->editor_height > 0) {
        *w = static_cast<uint32_t>(kp->editor_width);
        *h = static_cast<uint32_t>(kp->editor_height);
        keepsake_debug_log("keepsake: gui_get_size %ux%u\n", *w, *h);
        return true;
    }
    keepsake_debug_log("keepsake: gui_get_size unavailable\n");
    return false;
}

static bool gui_can_resize(const clap_plugin_t *) {
    keepsake_debug_log("keepsake: gui_can_resize -> 0\n");
    return false;
}
static bool gui_get_resize_hints(const clap_plugin_t *, clap_gui_resize_hints_t *) {
    keepsake_debug_log("keepsake: gui_get_resize_hints -> 0\n");
    return false;
}
static bool gui_adjust_size(const clap_plugin_t *, uint32_t *w, uint32_t *h) {
    keepsake_debug_log("keepsake: gui_adjust_size requested=%ux%u -> 0\n",
                       w ? *w : 0, h ? *h : 0);
    return false;
}
static bool gui_set_size(const clap_plugin_t *, uint32_t w, uint32_t h) {
    keepsake_debug_log("keepsake: gui_set_size %ux%u -> 0\n", w, h);
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
    keepsake_debug_log("keepsake: gui_set_parent floating=%d open=%d deferred\n",
                       kp->gui_is_floating ? 1 : 0, kp->editor_open ? 1 : 0);
    return true;
#else
    if (kp->gui_is_floating || kp->gui_embed_failed) {
        keepsake_debug_log("keepsake: gui_set_parent switching to floating path\n");
        return true;
    }
    uint64_t handle = 0;
    handle = static_cast<uint64_t>(window->x11);
    keepsake_debug_log("keepsake: gui_set_parent handle=%p\n",
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
    keepsake_debug_log("keepsake: gui_set_transient handle=%p\n",
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
    keepsake_debug_log("keepsake: gui_show floating=%d open=%d\n",
                       kp->gui_is_floating ? 1 : 0, kp->editor_open ? 1 : 0);
    if (kp->gui_is_floating && !kp->editor_open) {
        if (!send_and_wait(kp, IPC_OP_EDITOR_OPEN, nullptr, 0, nullptr,
                           GUI_OPEN_TIMEOUT_MS)) {
            keepsake_debug_log("keepsake: gui_show() editor open failed\n");
            return false;
        }
        kp->editor_open = true;
    }
    keepsake_debug_log("keepsake: gui_show success floating=%d open=%d\n",
                       kp->gui_is_floating ? 1 : 0, kp->editor_open ? 1 : 0);
    return true;
}

static bool gui_hide(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    keepsake_debug_log("keepsake: gui_hide open=%d\n", kp->editor_open ? 1 : 0);
    if (kp->editor_open && !kp->crashed) {
        send_and_wait(kp, IPC_OP_EDITOR_CLOSE);
        kp->editor_open = false;
    }
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
