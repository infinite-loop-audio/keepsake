//
// Keepsake CLAP plugin — GUI extension stub for non-macOS builds.
//

#include "plugin_internal.h"

static bool gui_is_api_supported(const clap_plugin_t *, const char *, bool) { return false; }
static bool gui_get_preferred_api(const clap_plugin_t *, const char **, bool *) { return false; }
static bool gui_create(const clap_plugin_t *, const char *, bool) { return false; }
static void gui_destroy(const clap_plugin_t *) {}
static bool gui_set_scale(const clap_plugin_t *, double) { return false; }
static bool gui_get_size(const clap_plugin_t *, uint32_t *, uint32_t *) { return false; }
static bool gui_can_resize(const clap_plugin_t *) { return false; }
static bool gui_get_resize_hints(const clap_plugin_t *, clap_gui_resize_hints_t *) { return false; }
static bool gui_adjust_size(const clap_plugin_t *, uint32_t *, uint32_t *) { return false; }
static bool gui_set_size(const clap_plugin_t *, uint32_t, uint32_t) { return false; }
static bool gui_set_parent(const clap_plugin_t *, const clap_window_t *) { return false; }
static bool gui_set_transient(const clap_plugin_t *, const clap_window_t *) { return false; }
static void gui_suggest_title(const clap_plugin_t *, const char *) {}
static bool gui_show(const clap_plugin_t *) { return false; }
static bool gui_hide(const clap_plugin_t *) { return false; }

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
