//
// Keepsake CLAP plugin — GUI extension.
//

#include "plugin_internal.h"

static bool gui_is_api_supported(const clap_plugin_t *plugin,
                                   const char *api, bool is_floating) {
    auto *kp = get(plugin);
    if (!kp->has_editor) return false;
#ifdef __APPLE__
    if (!is_floating) return false;
    return strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
#elif defined(_WIN32)
    return strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
#else
    return strcmp(api, CLAP_WINDOW_API_X11) == 0;
#endif
}

static bool gui_get_preferred_api(const clap_plugin_t *, const char **api,
                                    bool *is_floating) {
#ifdef __APPLE__
    *api = CLAP_WINDOW_API_COCOA;
    *is_floating = true;
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
    if (!kp->has_editor || kp->crashed) return false;
#ifdef __APPLE__
    if (!is_floating) return false;
#else
    (void)is_floating;
#endif
    kp->gui_is_floating = is_floating;
    return true;
}

static void gui_destroy(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    if (kp->editor_open && !kp->crashed) {
        send_and_wait(kp, IPC_OP_EDITOR_CLOSE);
        kp->editor_open = false;
    }
}

static bool gui_set_scale(const clap_plugin_t *, double) { return false; }

static bool gui_get_size(const clap_plugin_t *plugin, uint32_t *w, uint32_t *h) {
    auto *kp = get(plugin);
    if (kp->editor_width > 0 && kp->editor_height > 0) {
        *w = static_cast<uint32_t>(kp->editor_width);
        *h = static_cast<uint32_t>(kp->editor_height);
        return true;
    }
    return false;
}

static bool gui_can_resize(const clap_plugin_t *) { return false; }
static bool gui_get_resize_hints(const clap_plugin_t *, clap_gui_resize_hints_t *) { return false; }
static bool gui_adjust_size(const clap_plugin_t *, uint32_t *, uint32_t *) { return false; }
static bool gui_set_size(const clap_plugin_t *, uint32_t, uint32_t) { return false; }

static bool gui_set_parent(const clap_plugin_t *plugin, const clap_window_t *window) {
    auto *kp = get(plugin);
    if (kp->crashed || !kp->has_editor || !kp->bridge || !window) return false;

    uint64_t handle = 0;
#ifdef _WIN32
    handle = reinterpret_cast<uint64_t>(window->win32);
#elif defined(__linux__)
    handle = static_cast<uint64_t>(window->x11);
#else
    return false;
#endif

    if (!send_and_wait(kp, IPC_OP_EDITOR_SET_PARENT, &handle, sizeof(handle)))
        return false;
    kp->editor_open = true;
    return true;
}

static bool gui_set_transient(const clap_plugin_t *, const clap_window_t *) { return true; }
static void gui_suggest_title(const clap_plugin_t *, const char *) {}

static bool gui_show(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
    if (kp->crashed || !kp->has_editor) return false;
    if (kp->gui_is_floating && !kp->editor_open) {
        if (!send_and_wait(kp, IPC_OP_EDITOR_OPEN)) return false;
        kp->editor_open = true;
    }
    return true;
}

static bool gui_hide(const clap_plugin_t *plugin) {
    auto *kp = get(plugin);
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
