#import "bridge_gui_mac_internal.h"

#include "debug_log.h"
#include "ipc.h"

#include <cstdlib>
#include <cstring>
#include <ctime>

NSWindow *g_window = nil;
NSWindow *g_parentless_plugin_window = nil;
NSView *g_header = nil;
NSView *g_editor_container = nil;
id g_frame_change_observer = nil;
BridgeLoader *g_active_loader = nil;
bool g_editor_open = false;
bool g_iosurface_mode = false;
int g_current_width = 0;
int g_current_height = 0;
static ShmProcessControl *g_gui_status_ctrl = nullptr;
static bool g_logged_focus_state = false;
static bool g_last_app_active = false;
static bool g_last_window_key = false;
static bool g_last_window_main = false;
static id g_app_did_become_active_observer = nil;
static id g_app_did_resign_active_observer = nil;
static uint64_t g_last_editor_idle_us = 0;
static uint64_t g_last_periodic_capture_us = 0;
static uint64_t g_last_burst_capture_us = 0;
static int g_capture_burst_remaining = 0;

static bool gui_env_flag_enabled(const char *name, bool default_value) {
    const char *value = std::getenv(name);
    if (!value || !value[0]) return default_value;
    if (std::strcmp(value, "0") == 0 ||
        std::strcmp(value, "false") == 0 ||
        std::strcmp(value, "off") == 0) {
        return false;
    }
    if (std::strcmp(value, "1") == 0 ||
        std::strcmp(value, "true") == 0 ||
        std::strcmp(value, "on") == 0) {
        return true;
    }
    return default_value;
}

static bool gui_periodic_editor_idle_enabled() {
    return gui_env_flag_enabled("KEEPSAKE_MAC_PERIODIC_EDITOR_IDLE", true);
}

static uint64_t gui_editor_idle_interval_us() {
    const char *value = std::getenv("KEEPSAKE_MAC_EDITOR_IDLE_INTERVAL_US");
    if (!value || !value[0]) return 16000;
    const unsigned long long parsed = std::strtoull(value, nullptr, 10);
    return parsed > 0 ? static_cast<uint64_t>(parsed) : 16000;
}

static bool gui_periodic_capture_enabled() {
    return gui_env_flag_enabled("KEEPSAKE_MAC_PERIODIC_CAPTURE", true);
}

static uint64_t gui_periodic_capture_interval_us() {
    const char *value = std::getenv("KEEPSAKE_MAC_CAPTURE_INTERVAL_US");
    if (!value || !value[0]) return 0;
    const unsigned long long parsed = std::strtoull(value, nullptr, 10);
    return static_cast<uint64_t>(parsed);
}

static uint64_t gui_now_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL +
           static_cast<uint64_t>(ts.tv_nsec) / 1000ULL;
}

static int gui_capture_burst_frames() {
    const char *value = std::getenv("KEEPSAKE_MAC_CAPTURE_BURST_FRAMES");
    if (!value || !value[0]) return 6;
    return std::max(0, std::atoi(value));
}

static uint64_t gui_capture_burst_interval_us() {
    const char *value = std::getenv("KEEPSAKE_MAC_CAPTURE_BURST_INTERVAL_US");
    if (!value || !value[0]) return 33333;
    const unsigned long long parsed = std::strtoull(value, nullptr, 10);
    return parsed > 0 ? static_cast<uint64_t>(parsed) : 33333;
}

static void gui_log_focus_state(const char *phase) {
    const bool app_active = [NSApp isActive];
    const bool window_key = g_window ? [g_window isKeyWindow] : false;
    const bool window_main = g_window ? [g_window isMainWindow] : false;
    if (g_logged_focus_state &&
        app_active == g_last_app_active &&
        window_key == g_last_window_key &&
        window_main == g_last_window_main) {
        return;
    }
    g_logged_focus_state = true;
    g_last_app_active = app_active;
    g_last_window_key = window_key;
    g_last_window_main = window_main;
    keepsake_debug_log("bridge/mac: focus phase=%s app_active=%d window_key=%d window_main=%d parentless=%d\n",
                       phase,
                       app_active ? 1 : 0,
                       window_key ? 1 : 0,
                       window_main ? 1 : 0,
                       g_parentless_plugin_window ? 1 : 0);
}

static void gui_store_editor_state(uint32_t state) {
    if (!g_gui_status_ctrl) return;
    shm_store_release(&g_gui_status_ctrl->editor_state, state);
}

void gui_init() {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp finishLaunching];
    if (!g_app_did_become_active_observer) {
        g_app_did_become_active_observer =
            [[NSNotificationCenter defaultCenter]
                addObserverForName:NSApplicationDidBecomeActiveNotification
                object:NSApp
                queue:nil
                usingBlock:^(__unused NSNotification *note) {
                    keepsake_debug_log("bridge/mac: NSApp did become active\n");
                    gui_log_focus_state("nsapp-become-active");
                }];
    }
    if (!g_app_did_resign_active_observer) {
        g_app_did_resign_active_observer =
            [[NSNotificationCenter defaultCenter]
                addObserverForName:NSApplicationDidResignActiveNotification
                object:NSApp
                queue:nil
                usingBlock:^(__unused NSNotification *note) {
                    keepsake_debug_log("bridge/mac: NSApp did resign active\n");
                    gui_log_focus_state("nsapp-resign-active");
                }];
    }
}

bool gui_open_editor(BridgeLoader *loader, const EditorHeaderInfo &header) {
    if (!loader || !loader->has_editor()) return false;
    if (g_editor_open) return true;
    gui_store_editor_state(SHM_EDITOR_OPENING);
    const bool ok = gui_open_windowed_editor(loader, header);
    gui_store_editor_state(ok ? SHM_EDITOR_OPEN : SHM_EDITOR_FAILED);
    gui_log_focus_state(ok ? "open-ok" : "open-failed");
    return ok;
}

bool gui_open_editor_embedded(BridgeLoader * /*loader*/, uint64_t /*native_handle*/) {
    // macOS does not support cross-process NSView embedding.
    // Floating windows are the permanent approach on macOS.
    fprintf(stderr, "bridge: embedded editor not supported on macOS (use floating)\n");
    return false;
}

bool gui_stage_editor_parent(BridgeLoader *, uint64_t) {
    return false;
}

bool gui_has_pending_work() {
    return false;
}

void gui_get_editor_status(bool &open, bool &pending) {
    open = g_editor_open;
    pending = false;
}

void gui_set_status_shm(void *shm_ptr) {
    g_gui_status_ctrl = shm_ptr ? shm_control(shm_ptr) : nullptr;
}

void gui_publish_resize_request(int width, int height) {
    if (!g_gui_status_ctrl) return;
    g_gui_status_ctrl->editor_resize_width = width;
    g_gui_status_ctrl->editor_resize_height = height;
    const uint32_t serial = shm_load_acquire(&g_gui_status_ctrl->editor_resize_serial);
    shm_store_release(&g_gui_status_ctrl->editor_resize_serial, serial + 1);
}

void gui_request_capture_burst() {
    const int frames = gui_capture_burst_frames();
    if (frames <= 0) return;
    g_capture_burst_remaining = std::max(g_capture_burst_remaining, frames);
}

void gui_set_editor_transient(uint64_t) {}

void gui_close_editor(BridgeLoader *loader) {
    if (!g_editor_open) return;

    if (loader) loader->close_editor();
    else if (g_active_loader) g_active_loader->close_editor();

    gui_close_window_state();
    gui_close_iosurface_state();
    g_active_loader = nil;
    g_editor_open = false;
    g_last_editor_idle_us = 0;
    g_last_periodic_capture_us = 0;
    g_last_burst_capture_us = 0;
    g_capture_burst_remaining = 0;
    gui_store_editor_state(SHM_EDITOR_CLOSED);
    gui_log_focus_state("close");
}

bool gui_get_editor_rect(BridgeLoader *loader, int &width, int &height) {
    if (!loader) return false;
    return loader->get_editor_rect(width, height);
}

void gui_idle(BridgeLoader *loader) {
    if (!g_editor_open) return;

    gui_pump_pending_events(nil);
    gui_log_focus_state("idle");

    BridgeLoader *active = loader ? loader : g_active_loader;
    if (active) {
        const uint64_t now_us = gui_now_us();
        if (gui_periodic_editor_idle_enabled()) {
            const uint64_t interval_us = gui_editor_idle_interval_us();
            if (g_last_editor_idle_us == 0 ||
                now_us - g_last_editor_idle_us >= interval_us) {
                active->editor_idle();
                g_last_editor_idle_us = now_us;
            }
        }
        if (gui_periodic_capture_enabled()) {
            const uint64_t interval_us = gui_periodic_capture_interval_us();
            if (interval_us == 0 ||
                g_last_periodic_capture_us == 0 ||
                now_us - g_last_periodic_capture_us >= interval_us) {
                gui_capture_iosurface_if_needed();
                g_last_periodic_capture_us = now_us;
            }
        } else if (g_capture_burst_remaining > 0) {
            const uint64_t now_us = gui_now_us();
            const uint64_t interval_us = gui_capture_burst_interval_us();
            if (g_last_burst_capture_us == 0 ||
                now_us - g_last_burst_capture_us >= interval_us) {
                gui_capture_iosurface_if_needed();
                g_last_burst_capture_us = now_us;
                g_capture_burst_remaining -= 1;
            }
        }
    }

    if (g_window && ![g_window isVisible]) {
        gui_close_editor(loader);
    }
}

bool gui_is_open() {
    return g_editor_open;
}
