// host-probe-clap: tiny diagnostic CLAP plugin for comparing host lifecycle
// behavior between REAPER and the local windows-clap-host harness.

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr const char *kPluginId = "keepsake.host-probe";
constexpr const char *kPluginName = "Keepsake Host Probe";
constexpr const char *kVendor = "Infinite Loop Audio";
constexpr const char *kWindowClass = "KeepsakeHostProbeChild";
constexpr double kPi = 3.14159265358979323846;

std::mutex g_log_mutex;
FILE *g_log_file = nullptr;

double now_ms() {
    static LARGE_INTEGER freq = {};
    static bool init = false;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = true;
    }
    LARGE_INTEGER counter = {};
    QueryPerformanceCounter(&counter);
    return 1000.0 * static_cast<double>(counter.QuadPart) /
           static_cast<double>(freq.QuadPart);
}

const char *bool_name(bool value) {
    return value ? "1" : "0";
}

void open_log_file_once() {
    if (g_log_file) return;

    char path[MAX_PATH] = {};
    DWORD env_len = GetEnvironmentVariableA("KEEPSAKE_HOST_PROBE_LOG", path, MAX_PATH);
    if (env_len == 0 || env_len >= MAX_PATH) {
        char temp_dir[MAX_PATH] = {};
        GetTempPathA(MAX_PATH, temp_dir);
        std::snprintf(path, sizeof(path), "%skeepsake-host-probe.log", temp_dir);
    }
    g_log_file = std::fopen(path, "a");
}

void log_line(const char *fmt, ...) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    open_log_file_once();
    if (!g_log_file) return;

    std::fprintf(g_log_file, "[%10.3f][tid=%lu] ",
                 now_ms(), static_cast<unsigned long>(GetCurrentThreadId()));
    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_log_file, fmt, args);
    va_end(args);
    std::fflush(g_log_file);
}

void log_window_info(const char *label, HWND hwnd) {
    if (!hwnd) {
        log_line("%s hwnd=(null)\n", label);
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
    DWORD pid = 0;
    DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    log_line("%s hwnd=%p class='%s' style=0x%llx exstyle=0x%llx parent=%p owner=%p root=%p visible=%d enabled=%d tid=%lu pid=%lu rect=%ld,%ld,%ld,%ld\n",
             label,
             hwnd,
             klass,
             static_cast<long long>(style),
             static_cast<long long>(exstyle),
             parent,
             owner,
             root,
             IsWindowVisible(hwnd) ? 1 : 0,
             IsWindowEnabled(hwnd) ? 1 : 0,
             static_cast<unsigned long>(tid),
             static_cast<unsigned long>(pid),
             static_cast<long>(rc.left),
             static_cast<long>(rc.top),
             static_cast<long>(rc.right),
             static_cast<long>(rc.bottom));
}

void ensure_window_class() {
    static bool registered = false;
    if (registered) return;

    WNDCLASSA wc = {};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = kWindowClass;
    RegisterClassA(&wc);
    registered = true;
}

struct ProbePlugin {
    clap_plugin_t plugin = {};
    const clap_host_t *host = nullptr;
    bool active = false;
    bool processing = false;
    bool gui_created = false;
    bool gui_visible = false;
    double gui_scale = 1.0;
    HWND parent = nullptr;
    HWND child = nullptr;
    double sample_rate = 44100.0;
    uint32_t block_size = 512;
    double phase = 0.0;
    float param = 0.75f;
};

ProbePlugin *get_plugin(const clap_plugin_t *plugin) {
    return static_cast<ProbePlugin *>(plugin->plugin_data);
}

uint32_t in_events_size(const clap_input_events_t *list) {
    return list && list->ctx ? 0u : 0u;
}

const clap_event_header_t *in_events_get(const clap_input_events_t *, uint32_t) {
    return nullptr;
}

bool plugin_init(const clap_plugin_t *plugin) {
    auto *pp = get_plugin(plugin);
    log_line("plugin.init id=%s host_name=%s host_vendor=%s host_version=%s\n",
             plugin->desc ? plugin->desc->id : "(null)",
             pp->host && pp->host->name ? pp->host->name : "(null)",
             pp->host && pp->host->vendor ? pp->host->vendor : "(null)",
             pp->host && pp->host->version ? pp->host->version : "(null)");
    return true;
}

void plugin_destroy(const clap_plugin_t *plugin) {
    auto *pp = get_plugin(plugin);
    log_line("plugin.destroy gui_visible=%s active=%s processing=%s\n",
             bool_name(pp->gui_visible), bool_name(pp->active), bool_name(pp->processing));
    if (pp->child) DestroyWindow(pp->child);
    delete pp;
}

bool plugin_activate(const clap_plugin_t *plugin, double sample_rate,
                     uint32_t, uint32_t max_frames) {
    auto *pp = get_plugin(plugin);
    pp->active = true;
    pp->sample_rate = sample_rate;
    pp->block_size = max_frames;
    log_line("plugin.activate sample_rate=%.1f max_frames=%u gui_visible=%s\n",
             sample_rate, max_frames, bool_name(pp->gui_visible));
    return true;
}

void plugin_deactivate(const clap_plugin_t *plugin) {
    auto *pp = get_plugin(plugin);
    pp->active = false;
    log_line("plugin.deactivate\n");
}

bool plugin_start_processing(const clap_plugin_t *plugin) {
    auto *pp = get_plugin(plugin);
    pp->processing = true;
    log_line("plugin.start_processing\n");
    return true;
}

void plugin_stop_processing(const clap_plugin_t *plugin) {
    auto *pp = get_plugin(plugin);
    pp->processing = false;
    log_line("plugin.stop_processing\n");
}

void plugin_reset(const clap_plugin_t *plugin) {
    auto *pp = get_plugin(plugin);
    pp->phase = 0.0;
    log_line("plugin.reset\n");
}

clap_process_status plugin_process(const clap_plugin_t *plugin, const clap_process_t *process) {
    auto *pp = get_plugin(plugin);
    const uint32_t frames = process ? process->frames_count : 0;
    log_line("plugin.process frames=%u gui_visible=%s active=%s processing=%s\n",
             frames, bool_name(pp->gui_visible), bool_name(pp->active), bool_name(pp->processing));

    if (!process || process->audio_outputs_count == 0) {
        return CLAP_PROCESS_CONTINUE;
    }

    auto &out = process->audio_outputs[0];
    const double inc = (2.0 * kPi * 220.0) / (pp->sample_rate > 0.0 ? pp->sample_rate : 44100.0);
    for (uint32_t ch = 0; ch < out.channel_count; ++ch) {
        if (!out.data32 || !out.data32[ch]) continue;
        for (uint32_t i = 0; i < frames; ++i) {
            out.data32[ch][i] = 0.2f * std::sin(pp->phase);
            if (ch == 0) pp->phase += inc;
        }
    }
    if (pp->host && pp->host->request_callback && frames > 0 && (frames % 2) == 0) {
        pp->host->request_callback(pp->host);
    }
    return CLAP_PROCESS_CONTINUE;
}

const void *plugin_get_extension(const clap_plugin_t *plugin, const char *id);

void plugin_on_main_thread(const clap_plugin_t *) {
    log_line("plugin.on_main_thread\n");
}

bool gui_is_api_supported(const clap_plugin_t *, const char *api, bool is_floating) {
    log_line("gui.is_api_supported api=%s floating=%s\n", api ? api : "(null)", bool_name(is_floating));
    return api && std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0 && !is_floating;
}

bool gui_get_preferred_api(const clap_plugin_t *, const char **api, bool *is_floating) {
    if (api) *api = CLAP_WINDOW_API_WIN32;
    if (is_floating) *is_floating = false;
    log_line("gui.get_preferred_api\n");
    return true;
}

bool gui_create(const clap_plugin_t *plugin, const char *api, bool is_floating) {
    auto *pp = get_plugin(plugin);
    pp->gui_created = true;
    log_line("gui.create api=%s floating=%s\n", api ? api : "(null)", bool_name(is_floating));
    return api && std::strcmp(api, CLAP_WINDOW_API_WIN32) == 0 && !is_floating;
}

void gui_destroy(const clap_plugin_t *plugin) {
    auto *pp = get_plugin(plugin);
    log_line("gui.destroy visible=%s parent=%p child=%p\n",
             bool_name(pp->gui_visible), pp->parent, pp->child);
    if (pp->child) {
        DestroyWindow(pp->child);
        pp->child = nullptr;
    }
    pp->parent = nullptr;
    pp->gui_created = false;
    pp->gui_visible = false;
}

bool gui_set_scale(const clap_plugin_t *plugin, double scale) {
    auto *pp = get_plugin(plugin);
    pp->gui_scale = scale;
    log_line("gui.set_scale scale=%.3f\n", scale);
    return true;
}

bool gui_get_size(const clap_plugin_t *, uint32_t *w, uint32_t *h) {
    if (w) *w = static_cast<uint32_t>(std::lround(640.0 * 1.0));
    if (h) *h = static_cast<uint32_t>(std::lround(360.0 * 1.0));
    log_line("gui.get_size -> %u x %u\n", w ? *w : 0, h ? *h : 0);
    return true;
}

bool gui_can_resize(const clap_plugin_t *) {
    log_line("gui.can_resize -> 0\n");
    return false;
}

bool gui_get_resize_hints(const clap_plugin_t *, clap_gui_resize_hints_t *) {
    log_line("gui.get_resize_hints -> 0\n");
    return false;
}

bool gui_adjust_size(const clap_plugin_t *, uint32_t *w, uint32_t *h) {
    log_line("gui.adjust_size requested=%u x %u\n", w ? *w : 0, h ? *h : 0);
    return false;
}

bool gui_set_size(const clap_plugin_t *, uint32_t w, uint32_t h) {
    log_line("gui.set_size %u x %u\n", w, h);
    return false;
}

bool gui_set_parent(const clap_plugin_t *plugin, const clap_window_t *window) {
    auto *pp = get_plugin(plugin);
    pp->parent = window ? static_cast<HWND>(window->win32) : nullptr;
    ensure_window_class();
    log_line("gui.set_parent parent=%p api=%s\n",
             pp->parent, window && window->api ? window->api : "(null)");
    log_window_info("gui.parent", pp->parent);
    if (pp->child) {
        DestroyWindow(pp->child);
        pp->child = nullptr;
    }
    if (pp->parent) {
        RECT rect = {};
        GetClientRect(pp->parent, &rect);
        pp->child = CreateWindowExA(0, kWindowClass, "",
                                    WS_CHILD | WS_VISIBLE,
                                    0, 0,
                                    rect.right - rect.left,
                                    rect.bottom - rect.top,
                                    pp->parent, nullptr,
                                    GetModuleHandleA(nullptr), nullptr);
        log_window_info("gui.child", pp->child);
    }
    return true;
}

bool gui_set_transient(const clap_plugin_t *, const clap_window_t *window) {
    log_line("gui.set_transient handle=%p api=%s\n",
             window ? window->win32 : nullptr,
             window && window->api ? window->api : "(null)");
    return true;
}

void gui_suggest_title(const clap_plugin_t *, const char *title) {
    log_line("gui.suggest_title title=%s\n", title ? title : "(null)");
}

bool gui_show(const clap_plugin_t *plugin) {
    auto *pp = get_plugin(plugin);
    pp->gui_visible = true;
    if (pp->child) ShowWindow(pp->child, SW_SHOW);
    log_line("gui.show parent=%p child=%p\n", pp->parent, pp->child);
    log_window_info("gui.parent.show", pp->parent);
    log_window_info("gui.child.show", pp->child);
    return true;
}

bool gui_hide(const clap_plugin_t *plugin) {
    auto *pp = get_plugin(plugin);
    pp->gui_visible = false;
    if (pp->child) ShowWindow(pp->child, SW_HIDE);
    log_line("gui.hide parent=%p child=%p\n", pp->parent, pp->child);
    log_window_info("gui.parent.hide", pp->parent);
    log_window_info("gui.child.hide", pp->child);
    return true;
}

uint32_t params_count(const clap_plugin_t *) {
    log_line("params.count -> 1\n");
    return 1;
}

bool params_get_info(const clap_plugin_t *, uint32_t index, clap_param_info_t *info) {
    log_line("params.get_info index=%u\n", index);
    if (index != 0 || !info) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = 1;
    std::snprintf(info->name, sizeof(info->name), "Level");
    std::snprintf(info->module, sizeof(info->module), "Probe");
    info->default_value = 0.75;
    info->min_value = 0.0;
    info->max_value = 1.0;
    return true;
}

bool params_get_value(const clap_plugin_t *plugin, clap_id param_id, double *value) {
    auto *pp = get_plugin(plugin);
    log_line("params.get_value id=%u\n", param_id);
    if (param_id != 1 || !value) return false;
    *value = pp->param;
    return true;
}

bool params_value_to_text(const clap_plugin_t *, clap_id param_id, double value,
                          char *display, uint32_t size) {
    log_line("params.value_to_text id=%u value=%.3f\n", param_id, value);
    if (param_id != 1 || !display || size == 0) return false;
    std::snprintf(display, size, "%.2f", value);
    return true;
}

bool params_text_to_value(const clap_plugin_t *, clap_id param_id, const char *display,
                          double *value) {
    log_line("params.text_to_value id=%u text=%s\n", param_id, display ? display : "(null)");
    if (param_id != 1 || !display || !value) return false;
    *value = std::atof(display);
    return true;
}

void params_flush(const clap_plugin_t *, const clap_input_events_t *, const clap_output_events_t *) {
    log_line("params.flush\n");
}

bool state_save(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
    auto *pp = get_plugin(plugin);
    log_line("state.save param=%.3f\n", pp->param);
    if (!stream || !stream->write) return false;
    const int64_t written = stream->write(stream, &pp->param, sizeof(pp->param));
    return written == static_cast<int64_t>(sizeof(pp->param));
}

bool state_load(const clap_plugin_t *plugin, const clap_istream_t *stream) {
    auto *pp = get_plugin(plugin);
    log_line("state.load\n");
    if (!stream || !stream->read) return false;
    const int64_t read = stream->read(stream, &pp->param, sizeof(pp->param));
    return read == static_cast<int64_t>(sizeof(pp->param));
}

uint32_t audio_ports_count(const clap_plugin_t *, bool is_input) {
    log_line("audio_ports.count is_input=%s\n", bool_name(is_input));
    return is_input ? 0 : 1;
}

bool audio_ports_get(const clap_plugin_t *, uint32_t index, bool is_input,
                     clap_audio_port_info_t *info) {
    log_line("audio_ports.get index=%u is_input=%s\n", index, bool_name(is_input));
    if (index != 0 || is_input || !info) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = 1;
    std::snprintf(info->name, sizeof(info->name), "Main Out");
    info->channel_count = 2;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

uint32_t note_ports_count(const clap_plugin_t *, bool is_input) {
    log_line("note_ports.count is_input=%s\n", bool_name(is_input));
    return is_input ? 1 : 0;
}

bool note_ports_get(const clap_plugin_t *, uint32_t index, bool is_input,
                    clap_note_port_info_t *info) {
    log_line("note_ports.get index=%u is_input=%s\n", index, bool_name(is_input));
    if (index != 0 || !is_input || !info) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = 1;
    info->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
    std::snprintf(info->name, sizeof(info->name), "Note In");
    return true;
}

const clap_plugin_gui_t s_gui = {
    gui_is_api_supported,
    gui_get_preferred_api,
    gui_create,
    gui_destroy,
    gui_set_scale,
    gui_get_size,
    gui_can_resize,
    gui_get_resize_hints,
    gui_adjust_size,
    gui_set_size,
    gui_set_parent,
    gui_set_transient,
    gui_suggest_title,
    gui_show,
    gui_hide,
};

const clap_plugin_params_t s_params = {
    params_count,
    params_get_info,
    params_get_value,
    params_value_to_text,
    params_text_to_value,
    params_flush,
};

const clap_plugin_state_t s_state = {
    state_save,
    state_load,
};

const clap_plugin_audio_ports_t s_audio_ports = {
    audio_ports_count,
    audio_ports_get,
};

const clap_plugin_note_ports_t s_note_ports = {
    note_ports_count,
    note_ports_get,
};

const void *plugin_get_extension(const clap_plugin_t *, const char *id) {
    log_line("plugin.get_extension id=%s\n", id ? id : "(null)");
    if (!id) return nullptr;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &s_gui;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &s_params;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &s_state;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &s_audio_ports;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &s_note_ports;
    return nullptr;
}

const clap_plugin_descriptor_t s_desc = {
    CLAP_VERSION,
    kPluginId,
    kPluginName,
    kVendor,
    "",
    "",
    "",
    "1.0",
    "Diagnostic CLAP plugin for host protocol logging",
    nullptr
};

const clap_plugin_t *factory_create_plugin(const clap_plugin_factory_t *,
                                           const clap_host_t *host,
                                           const char *plugin_id) {
    log_line("factory.create_plugin id=%s host_name=%s\n",
             plugin_id ? plugin_id : "(null)",
             host && host->name ? host->name : "(null)");
    if (!plugin_id || std::strcmp(plugin_id, kPluginId) != 0) return nullptr;
    auto *pp = new ProbePlugin();
    pp->host = host;
    pp->plugin.desc = &s_desc;
    pp->plugin.plugin_data = pp;
    pp->plugin.init = plugin_init;
    pp->plugin.destroy = plugin_destroy;
    pp->plugin.activate = plugin_activate;
    pp->plugin.deactivate = plugin_deactivate;
    pp->plugin.start_processing = plugin_start_processing;
    pp->plugin.stop_processing = plugin_stop_processing;
    pp->plugin.reset = plugin_reset;
    pp->plugin.process = plugin_process;
    pp->plugin.get_extension = plugin_get_extension;
    pp->plugin.on_main_thread = plugin_on_main_thread;
    return &pp->plugin;
}

uint32_t factory_get_plugin_count(const clap_plugin_factory_t *) {
    log_line("factory.get_plugin_count\n");
    return 1;
}

const clap_plugin_descriptor_t *factory_get_plugin_descriptor(const clap_plugin_factory_t *,
                                                              uint32_t index) {
    log_line("factory.get_plugin_descriptor index=%u\n", index);
    return index == 0 ? &s_desc : nullptr;
}

const clap_plugin_factory_t s_factory = {
    factory_get_plugin_count,
    factory_get_plugin_descriptor,
    factory_create_plugin,
};

bool entry_init(const char *plugin_path) {
    log_line("entry.init plugin_path=%s\n", plugin_path ? plugin_path : "(null)");
    return true;
}

void entry_deinit() {
    log_line("entry.deinit\n");
}

const void *entry_get_factory(const char *factory_id) {
    log_line("entry.get_factory id=%s\n", factory_id ? factory_id : "(null)");
    if (factory_id && std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &s_factory;
    }
    return nullptr;
}

} // namespace

extern "C" {

CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION,
    entry_init,
    entry_deinit,
    entry_get_factory,
};

} // extern "C"
