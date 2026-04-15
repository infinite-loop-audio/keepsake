// windows-clap-host: tiny Windows CLAP host harness for reproducing Keepsake
// lifecycle behavior without REAPER.

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/latency.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char *kWindowClass = "KeepsakeClapHostWindow";
constexpr const char *kReaperWrapClass = "reaperPluginHostWrapProc";

struct Options {
    std::string clap_path;
    std::string plugin_id;
    std::string vst_path;
    std::string mode = "gui-first";
    bool open_ui = false;
    bool run_transport = false;
    bool activate_before_ui = false;
    bool show_parent_before_set_parent = false;
    bool reaper_parent_shape = false;
    bool process_off_main_thread = false;
    bool promote_parent_after_show = false;
    bool query_reaper_extensions = false;
    bool restart_processing_after_ui = false;
    int state_saves_before_ui = 0;
    int state_saves_after_ui = 0;
    int process_thread_count = 1;
    int restart_process_blocks = 12;
    int activate_delay_ms = 0;
    int width = 900;
    int height = 700;
    int ui_timeout_ms = 5000;
    int process_blocks = 96;
    int block_size = 512;
    int sample_rate = 44100;
    int parent_promote_delay_ms = 50;
    double scale = 2.0;
};

struct HostState {
    clap_host_t host = {};
    std::atomic<bool> callback_requested{false};
    std::atomic<bool> process_requested{false};
    std::atomic<bool> restart_requested{false};
};

struct MidiEventList {
    clap_input_events_t iface = {};
    std::vector<clap_event_midi_t> events;
    std::vector<const clap_event_header_t *> headers;
};

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

void log_line(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::fflush(stdout);
}

bool configure_scan_override(const std::string &vst_path) {
    if (vst_path.empty()) return true;

    const fs::path src(vst_path);
    if (!fs::exists(src)) return false;

    char temp_root[MAX_PATH] = {};
    if (!GetTempPathA(MAX_PATH, temp_root) || !temp_root[0]) return false;

    char temp_dir[MAX_PATH] = {};
    if (!GetTempFileNameA(temp_root, "kch", 0, temp_dir)) return false;
    DeleteFileA(temp_dir);
    if (!CreateDirectoryA(temp_dir, nullptr)) return false;

    const fs::path dst = fs::path(temp_dir) / src.filename();
    std::error_code ec;
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) return false;

    return SetEnvironmentVariableA("KEEPSAKE_VST2_PATH", temp_dir) != FALSE;
}

bool parse_args(int argc, char *argv[], Options &opts) {
    if (argc < 3) return false;
    opts.clap_path = argv[1];
    opts.plugin_id = argv[2];
    for (int i = 3; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--vst-path") == 0 && i + 1 < argc) {
            opts.vst_path = argv[++i];
        } else if (strcmp(arg, "--mode") == 0 && i + 1 < argc) {
            opts.mode = argv[++i];
        } else if (strcmp(arg, "--open-ui") == 0) {
            opts.open_ui = true;
        } else if (strcmp(arg, "--run-transport") == 0) {
            opts.run_transport = true;
        } else if (strcmp(arg, "--activate-before-ui") == 0) {
            opts.activate_before_ui = true;
        } else if (strcmp(arg, "--show-parent-before-set-parent") == 0) {
            opts.show_parent_before_set_parent = true;
        } else if (strcmp(arg, "--reaper-parent-shape") == 0) {
            opts.reaper_parent_shape = true;
        } else if (strcmp(arg, "--process-off-main-thread") == 0) {
            opts.process_off_main_thread = true;
        } else if (strcmp(arg, "--promote-parent-after-show") == 0) {
            opts.promote_parent_after_show = true;
        } else if (strcmp(arg, "--activate-delay-ms") == 0 && i + 1 < argc) {
            opts.activate_delay_ms = std::atoi(argv[++i]);
        } else if (strcmp(arg, "--parent-promote-delay-ms") == 0 && i + 1 < argc) {
            opts.parent_promote_delay_ms = std::atoi(argv[++i]);
        } else if (strcmp(arg, "--query-reaper-extensions") == 0) {
            opts.query_reaper_extensions = true;
        } else if (strcmp(arg, "--restart-processing-after-ui") == 0) {
            opts.restart_processing_after_ui = true;
        } else if (strcmp(arg, "--state-saves-before-ui") == 0 && i + 1 < argc) {
            opts.state_saves_before_ui = std::atoi(argv[++i]);
        } else if (strcmp(arg, "--state-saves-after-ui") == 0 && i + 1 < argc) {
            opts.state_saves_after_ui = std::atoi(argv[++i]);
        } else if (strcmp(arg, "--process-thread-count") == 0 && i + 1 < argc) {
            opts.process_thread_count = std::atoi(argv[++i]);
        } else if (strcmp(arg, "--restart-process-blocks") == 0 && i + 1 < argc) {
            opts.restart_process_blocks = std::atoi(argv[++i]);
        } else if (strcmp(arg, "--ui-timeout-ms") == 0 && i + 1 < argc) {
            opts.ui_timeout_ms = std::atoi(argv[++i]);
        } else if (strcmp(arg, "--process-blocks") == 0 && i + 1 < argc) {
            opts.process_blocks = std::atoi(argv[++i]);
        } else if (strcmp(arg, "--block-size") == 0 && i + 1 < argc) {
            opts.block_size = std::atoi(argv[++i]);
        } else if (strcmp(arg, "--sample-rate") == 0 && i + 1 < argc) {
            opts.sample_rate = std::atoi(argv[++i]);
        } else if (strcmp(arg, "--scale") == 0 && i + 1 < argc) {
            opts.scale = std::atof(argv[++i]);
        } else if (strcmp(arg, "--width") == 0 && i + 1 < argc) {
            opts.width = std::atoi(argv[++i]);
        } else if (strcmp(arg, "--height") == 0 && i + 1 < argc) {
            opts.height = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", arg);
            return false;
        }
    }
    return true;
}

void apply_mode_defaults(Options &opts) {
    if (opts.mode == "gui-first") {
        opts.activate_before_ui = false;
        return;
    }
    if (opts.mode == "activate-first") {
        opts.activate_before_ui = true;
        return;
    }
    if (opts.mode == "gui-first-parent-visible") {
        opts.activate_before_ui = false;
        opts.show_parent_before_set_parent = true;
        return;
    }
    if (opts.mode == "activate-first-parent-visible") {
        opts.activate_before_ui = true;
        opts.show_parent_before_set_parent = true;
        return;
    }
    if (opts.mode == "gui-first-delayed-activate") {
        opts.activate_before_ui = false;
        if (opts.activate_delay_ms <= 0) opts.activate_delay_ms = 250;
        return;
    }
    if (opts.mode == "reaper-ish") {
        opts.activate_before_ui = true;
        opts.reaper_parent_shape = true;
        opts.process_off_main_thread = true;
        opts.query_reaper_extensions = true;
        opts.state_saves_before_ui = 2;
        opts.state_saves_after_ui = 1;
        opts.restart_processing_after_ui = true;
        if (opts.process_thread_count < 4) opts.process_thread_count = 4;
        if (opts.restart_process_blocks <= 0) opts.restart_process_blocks = 12;
        return;
    }
}

LRESULT CALLBACK host_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

bool ensure_window_class() {
    static bool registered = false;
    if (registered) return true;

    const char *class_names[] = { kWindowClass, kReaperWrapClass };
    for (const char *class_name : class_names) {
        WNDCLASSA wc = {};
        wc.lpfnWndProc = host_wnd_proc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = class_name;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }
    registered = true;
    return true;
}

void pump_messages() {
    MSG msg = {};
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

uint32_t midi_size(const clap_input_events_t *list) {
    auto *events = static_cast<const MidiEventList *>(list->ctx);
    return static_cast<uint32_t>(events->headers.size());
}

const clap_event_header_t *midi_get(const clap_input_events_t *list, uint32_t index) {
    auto *events = static_cast<const MidiEventList *>(list->ctx);
    if (index >= events->headers.size()) return nullptr;
    return events->headers[index];
}

void midi_clear(MidiEventList &list) {
    list.events.clear();
    list.headers.clear();
}

void midi_add_note(MidiEventList &list, bool note_on, uint32_t time, uint8_t key, uint8_t velocity) {
    clap_event_midi_t event = {};
    event.header.size = sizeof(event);
    event.header.time = time;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_MIDI;
    event.header.flags = 0;
    event.data[0] = static_cast<uint8_t>((note_on ? 0x90 : 0x80) | 0);
    event.data[1] = key;
    event.data[2] = velocity;
    event.data[3] = 0;
    list.events.push_back(event);
    list.headers.push_back(&list.events.back().header);
}

void init_midi_list(MidiEventList &list) {
    list.iface.ctx = &list;
    list.iface.size = midi_size;
    list.iface.get = midi_get;
}

const void *host_get_extension(const clap_host_t *, const char *) {
    return nullptr;
}

void host_request_restart(const clap_host_t *host) {
    auto *state = static_cast<HostState *>(host->host_data);
    state->restart_requested.store(true, std::memory_order_relaxed);
}

void host_request_process(const clap_host_t *host) {
    auto *state = static_cast<HostState *>(host->host_data);
    state->process_requested.store(true, std::memory_order_relaxed);
}

void host_request_callback(const clap_host_t *host) {
    auto *state = static_cast<HostState *>(host->host_data);
    state->callback_requested.store(true, std::memory_order_relaxed);
}

void init_host(HostState &state) {
    state.host.clap_version = CLAP_VERSION;
    state.host.host_data = &state;
    state.host.name = "keepsake-windows-clap-host";
    state.host.vendor = "Infinite Loop Audio";
    state.host.url = "";
    state.host.version = "1.0";
    state.host.get_extension = host_get_extension;
    state.host.request_restart = host_request_restart;
    state.host.request_process = host_request_process;
    state.host.request_callback = host_request_callback;
}

bool pump_callbacks_for(const clap_plugin_t *plugin, HostState &host, int timeout_ms) {
    const double deadline = now_ms() + timeout_ms;
    while (now_ms() < deadline) {
        pump_messages();
        if (host.callback_requested.exchange(false, std::memory_order_relaxed)) {
            plugin->on_main_thread(plugin);
        }
        Sleep(10);
    }
    return true;
}

struct MemoryOStream {
    clap_ostream_t iface = {};
    std::vector<uint8_t> bytes;
};

int64_t memory_stream_write(const clap_ostream_t *stream, const void *buffer, uint64_t size) {
    auto *out = static_cast<MemoryOStream *>(stream->ctx);
    const auto *src = static_cast<const uint8_t *>(buffer);
    out->bytes.insert(out->bytes.end(), src, src + size);
    return static_cast<int64_t>(size);
}

void init_memory_stream(MemoryOStream &stream) {
    stream.iface.ctx = &stream;
    stream.iface.write = memory_stream_write;
}

void run_main_thread_callback(const clap_plugin_t *plugin, HostState &host) {
    if (host.callback_requested.exchange(false, std::memory_order_relaxed)) {
        plugin->on_main_thread(plugin);
    }
}

void save_state_snapshot(const clap_plugin_t *plugin, const clap_plugin_state_t *state, int index) {
    if (!state || !state->save) return;
    MemoryOStream stream;
    init_memory_stream(stream);
    const double t0 = now_ms();
    const bool ok = state->save(plugin, &stream.iface);
    log_line("[state.save %d] %.1f ms ok=%d bytes=%zu\n",
             index, now_ms() - t0, ok ? 1 : 0, stream.bytes.size());
}

struct ExtensionRefs {
    const clap_plugin_gui_t *gui = nullptr;
    const clap_plugin_audio_ports_t *audio_ports = nullptr;
    const clap_plugin_params_t *params = nullptr;
    const clap_plugin_state_t *state = nullptr;
    const clap_plugin_latency_t *latency = nullptr;
    const clap_plugin_note_ports_t *note_ports = nullptr;
};

ExtensionRefs query_extensions(const clap_plugin_t *plugin, bool reaperish) {
    ExtensionRefs refs;
    if (reaperish) {
        const char *ids[] = {
            "com.celemony.ara.plugin_extension.draft/1",
            CLAP_EXT_PARAMS,
            CLAP_EXT_GUI,
            CLAP_EXT_TAIL,
            CLAP_EXT_LATENCY,
            CLAP_EXT_NOTE_NAME,
            CLAP_EXT_TIMER_SUPPORT,
            "clap.gain-adjustment-metering/0",
            "clap.track-info/1",
            "clap.track-info.draft/1",
            CLAP_EXT_THREAD_POOL,
            "cockos.reaper_embedui",
            "clap.preset-load/2",
            "clap.preset-load.draft/2",
            CLAP_EXT_STATE,
            CLAP_EXT_NOTE_PORTS,
            CLAP_EXT_AUDIO_PORTS,
        };
        for (const char *id : ids) {
            const void *ext = plugin->get_extension(plugin, id);
            if (std::strcmp(id, CLAP_EXT_GUI) == 0) {
                refs.gui = static_cast<const clap_plugin_gui_t *>(ext);
            } else if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
                refs.audio_ports = static_cast<const clap_plugin_audio_ports_t *>(ext);
            } else if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
                refs.params = static_cast<const clap_plugin_params_t *>(ext);
            } else if (std::strcmp(id, CLAP_EXT_STATE) == 0) {
                refs.state = static_cast<const clap_plugin_state_t *>(ext);
            } else if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) {
                refs.latency = static_cast<const clap_plugin_latency_t *>(ext);
            } else if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) {
                refs.note_ports = static_cast<const clap_plugin_note_ports_t *>(ext);
            }
        }
    } else {
        refs.gui = static_cast<const clap_plugin_gui_t *>(plugin->get_extension(plugin, CLAP_EXT_GUI));
        refs.audio_ports = static_cast<const clap_plugin_audio_ports_t *>(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
        refs.params = static_cast<const clap_plugin_params_t *>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        refs.state = static_cast<const clap_plugin_state_t *>(plugin->get_extension(plugin, CLAP_EXT_STATE));
        refs.latency = static_cast<const clap_plugin_latency_t *>(plugin->get_extension(plugin, CLAP_EXT_LATENCY));
        refs.note_ports = static_cast<const clap_plugin_note_ports_t *>(plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
    }
    return refs;
}

float process_audio(const clap_plugin_t *plugin, const Options &opts, HostState &host, int blocks) {
    std::vector<float> out_l(static_cast<size_t>(opts.block_size), 0.0f);
    std::vector<float> out_r(static_cast<size_t>(opts.block_size), 0.0f);
    float *out_ptrs[2] = { out_l.data(), out_r.data() };

    clap_audio_buffer_t out_buf = {};
    out_buf.channel_count = 2;
    out_buf.data32 = out_ptrs;

    MidiEventList midi;
    init_midi_list(midi);

    clap_process_t process = {};
    process.frames_count = static_cast<uint32_t>(opts.block_size);
    process.audio_outputs = &out_buf;
    process.audio_outputs_count = 1;
    process.in_events = &midi.iface;

    float peak = 0.0f;
    for (int i = 0; i < blocks; ++i) {
        midi_clear(midi);
        if (i == 0) midi_add_note(midi, true, 0, 60, 100);
        if (i == 24) midi_add_note(midi, false, 0, 60, 0);

        std::fill(out_l.begin(), out_l.end(), 0.0f);
        std::fill(out_r.begin(), out_r.end(), 0.0f);

        const double t0 = now_ms();
        clap_process_status status = CLAP_PROCESS_SLEEP;
        if (opts.process_off_main_thread) {
            std::atomic<bool> done{false};
            std::thread worker([&]() {
                status = plugin->process(plugin, &process);
                done.store(true, std::memory_order_release);
            });
            while (!done.load(std::memory_order_acquire)) {
                pump_messages();
                run_main_thread_callback(plugin, host);
                Sleep(1);
            }
            worker.join();
        } else if (opts.process_thread_count <= 1) {
            status = plugin->process(plugin, &process);
        } else {
            std::thread worker([&]() {
                status = plugin->process(plugin, &process);
            });
            worker.join();
        }
        const double elapsed = now_ms() - t0;

        float block_peak = 0.0f;
        for (int s = 0; s < opts.block_size; ++s) {
            block_peak = (std::max)(block_peak, std::fabs(out_l[static_cast<size_t>(s)]));
            block_peak = (std::max)(block_peak, std::fabs(out_r[static_cast<size_t>(s)]));
        }
        peak = (std::max)(peak, block_peak);
        if (i < 4 || block_peak > 0.001f || (i % 24) == 0) {
            log_line("[process %03d] %.1f ms status=%d peak=%.6f\n",
                     i, elapsed, static_cast<int>(status), block_peak);
        }

        run_main_thread_callback(plugin, host);
        pump_messages();
        Sleep(static_cast<DWORD>((1000LL * opts.block_size) / (std::max)(1, opts.sample_rate)));
    }

    return peak;
}

std::string clap_binary_path(const std::string &clap_path) {
    if (clap_path.size() >= 5 &&
        _stricmp(clap_path.c_str() + clap_path.size() - 5, ".clap") == 0) {
        return clap_path;
    }
    return clap_path;
}

} // namespace

int main(int argc, char *argv[]) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    Options opts;
    if (!parse_args(argc, argv, opts)) {
        std::fprintf(stderr,
                     "usage: %s <clap-path> <plugin-id> [--mode gui-first|activate-first|gui-first-parent-visible|activate-first-parent-visible|gui-first-delayed-activate|reaper-ish] [--vst-path PATH] [--open-ui] [--run-transport] [--reaper-parent-shape] [--process-off-main-thread] [--promote-parent-after-show] [--parent-promote-delay-ms N] [--query-reaper-extensions] [--state-saves-before-ui N] [--state-saves-after-ui N] [--process-thread-count N] [--restart-processing-after-ui]\n",
                     argv[0]);
        return 1;
    }
    apply_mode_defaults(opts);

    if (!configure_scan_override(opts.vst_path)) {
        std::fprintf(stderr, "warning: failed to configure scan override for '%s'\n",
                     opts.vst_path.c_str());
    }

    const std::string clap_binary = clap_binary_path(opts.clap_path);
    HMODULE lib = LoadLibraryA(clap_binary.c_str());
    if (!lib) {
        std::fprintf(stderr, "failed to load CLAP binary: %s\n", clap_binary.c_str());
        return 1;
    }

    auto *entry = reinterpret_cast<const clap_plugin_entry_t *>(
        GetProcAddress(lib, "clap_entry"));
    if (!entry) {
        std::fprintf(stderr, "no clap_entry\n");
        return 1;
    }

    log_line("=== Windows CLAP Host ===\n");
    log_line("clap=%s\nplugin_id=%s\n", opts.clap_path.c_str(), opts.plugin_id.c_str());
    if (!opts.vst_path.empty()) log_line("vst_path=%s\n", opts.vst_path.c_str());
    log_line("mode=%s activate_before_ui=%d parent_visible_before_set_parent=%d reaper_parent_shape=%d process_off_main_thread=%d promote_parent_after_show=%d parent_promote_delay_ms=%d activate_delay_ms=%d query_reaper_extensions=%d state_saves_before_ui=%d state_saves_after_ui=%d process_thread_count=%d restart_processing_after_ui=%d restart_process_blocks=%d\n",
             opts.mode.c_str(),
             opts.activate_before_ui ? 1 : 0,
             opts.show_parent_before_set_parent ? 1 : 0,
             opts.reaper_parent_shape ? 1 : 0,
             opts.process_off_main_thread ? 1 : 0,
             opts.promote_parent_after_show ? 1 : 0,
             opts.parent_promote_delay_ms,
             opts.activate_delay_ms,
             opts.query_reaper_extensions ? 1 : 0,
             opts.state_saves_before_ui,
             opts.state_saves_after_ui,
             opts.process_thread_count,
             opts.restart_processing_after_ui ? 1 : 0,
             opts.restart_process_blocks);

    double t0 = now_ms();
    if (!entry->init(opts.clap_path.c_str())) {
        std::fprintf(stderr, "entry.init failed\n");
        return 1;
    }
    log_line("[entry.init] %.1f ms\n", now_ms() - t0);

    auto *factory = reinterpret_cast<const clap_plugin_factory_t *>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory) {
        std::fprintf(stderr, "no plugin factory\n");
        return 1;
    }

    const clap_plugin_descriptor_t *desc = nullptr;
    for (uint32_t i = 0; i < factory->get_plugin_count(factory); ++i) {
        auto *candidate = factory->get_plugin_descriptor(factory, i);
        if (candidate && std::strcmp(candidate->id, opts.plugin_id.c_str()) == 0) {
            desc = candidate;
            break;
        }
    }
    if (!desc) {
        std::fprintf(stderr, "plugin '%s' not found\n", opts.plugin_id.c_str());
        for (uint32_t i = 0; i < factory->get_plugin_count(factory); ++i) {
            auto *candidate = factory->get_plugin_descriptor(factory, i);
            if (candidate) log_line("  available: %s -- %s\n", candidate->id, candidate->name);
        }
        return 1;
    }
    log_line("[factory] found %s -- %s\n", desc->id, desc->name);

    HostState host_state;
    init_host(host_state);

    t0 = now_ms();
    const clap_plugin_t *plugin = factory->create_plugin(factory, &host_state.host, desc->id);
    if (!plugin) {
        std::fprintf(stderr, "create_plugin failed\n");
        return 1;
    }
    log_line("[create_plugin] %.1f ms\n", now_ms() - t0);

    t0 = now_ms();
    if (!plugin->init(plugin)) {
        std::fprintf(stderr, "plugin.init failed\n");
        plugin->destroy(plugin);
        return 1;
    }
    log_line("[plugin.init] %.1f ms\n", now_ms() - t0);

    const auto refs = query_extensions(plugin, opts.query_reaper_extensions);
    auto *gui = refs.gui;
    auto *audio_ports = refs.audio_ports;
    auto *params = refs.params;
    auto *state = refs.state;
    auto *latency = refs.latency;
    auto *note_ports = refs.note_ports;

    log_line("[extensions] gui=%d audio_ports=%d params=%d state=%d latency=%d note_ports=%d\n",
             gui ? 1 : 0, audio_ports ? 1 : 0, params ? 1 : 0,
             state ? 1 : 0, latency ? 1 : 0, note_ports ? 1 : 0);

    bool activated = false;
    bool processing = false;
    auto start_audio = [&]() -> bool {
        if (activated) return true;
        t0 = now_ms();
        if (!plugin->activate(plugin, static_cast<double>(opts.sample_rate), 32,
                              static_cast<uint32_t>(opts.block_size))) {
            std::fprintf(stderr, "activate failed\n");
            return false;
        }
        activated = true;
        log_line("[activate] %.1f ms\n", now_ms() - t0);

        t0 = now_ms();
        if (!plugin->start_processing(plugin)) {
            std::fprintf(stderr, "start_processing failed\n");
            return false;
        }
        processing = true;
        log_line("[start_processing] %.1f ms\n", now_ms() - t0);
        return true;
    };

    if (opts.activate_before_ui && !start_audio()) {
        plugin->destroy(plugin);
        entry->deinit();
        return 1;
    }

    for (int i = 0; i < opts.state_saves_before_ui; ++i) {
        save_state_snapshot(plugin, state, i + 1);
    }

    HWND frame = nullptr;
    HWND parent = nullptr;
    uint32_t ui_w = static_cast<uint32_t>(opts.width);
    uint32_t ui_h = static_cast<uint32_t>(opts.height);

    if (opts.open_ui) {
        if (!gui) {
            std::fprintf(stderr, "plugin has no GUI extension\n");
            plugin->destroy(plugin);
            return 1;
        }

        if (!ensure_window_class()) {
            std::fprintf(stderr, "failed to register window class\n");
            plugin->destroy(plugin);
            return 1;
        }

        t0 = now_ms();
        if (!gui->create(plugin, CLAP_WINDOW_API_WIN32, false)) {
            std::fprintf(stderr, "gui.create failed\n");
            plugin->destroy(plugin);
            return 1;
        }
        log_line("[gui.create] %.1f ms\n", now_ms() - t0);

        t0 = now_ms();
        gui->set_scale(plugin, opts.scale);
        log_line("[gui.set_scale] %.1f ms scale=%.2f\n", now_ms() - t0, opts.scale);

        t0 = now_ms();
        if (gui->get_size(plugin, &ui_w, &ui_h)) {
            log_line("[gui.get_size] %.1f ms size=%ux%u\n", now_ms() - t0, ui_w, ui_h);
        } else {
            log_line("[gui.get_size] %.1f ms unavailable\n", now_ms() - t0);
        }

        t0 = now_ms();
        const bool can_resize = gui->can_resize ? gui->can_resize(plugin) : false;
        log_line("[gui.can_resize] %.1f ms -> %d\n", now_ms() - t0, can_resize ? 1 : 0);

        const DWORD frame_style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        frame = CreateWindowExA(0, kWindowClass, "Keepsake Windows CLAP Host",
                                frame_style, CW_USEDEFAULT, CW_USEDEFAULT,
                                static_cast<int>(ui_w) + 80, static_cast<int>(ui_h) + 120,
                                nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
        if (!frame) {
            std::fprintf(stderr, "failed to create frame window\n");
            gui->destroy(plugin);
            plugin->destroy(plugin);
            return 1;
        }
        const DWORD parent_exstyle = opts.reaper_parent_shape ? WS_EX_CONTROLPARENT : 0;
        const char *parent_class = opts.reaper_parent_shape ? kReaperWrapClass : "STATIC";
        const DWORD parent_style = opts.reaper_parent_shape
            ? (WS_CHILD | (opts.show_parent_before_set_parent ? WS_VISIBLE : 0))
            : (WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
               (opts.show_parent_before_set_parent ? WS_VISIBLE : 0));
        parent = CreateWindowExA(parent_exstyle, parent_class, "",
                                 parent_style,
                                 0, 0, static_cast<int>(ui_w), static_cast<int>(ui_h),
                                 frame, nullptr, GetModuleHandleA(nullptr), nullptr);
        if (!parent) {
            std::fprintf(stderr, "failed to create parent window\n");
            DestroyWindow(frame);
            gui->destroy(plugin);
            plugin->destroy(plugin);
            return 1;
        }
        if (opts.show_parent_before_set_parent) {
            ShowWindow(parent, SW_SHOW);
            ShowWindow(frame, SW_SHOW);
            UpdateWindow(frame);
        }

        clap_window_t window = {};
        window.api = CLAP_WINDOW_API_WIN32;
        window.win32 = parent;

        t0 = now_ms();
        if (!gui->set_parent(plugin, &window)) {
            std::fprintf(stderr, "gui.set_parent failed\n");
            DestroyWindow(parent);
            DestroyWindow(frame);
            gui->destroy(plugin);
            plugin->destroy(plugin);
            return 1;
        }
        log_line("[gui.set_parent] %.1f ms hwnd=%p\n", now_ms() - t0, parent);

        if (!opts.show_parent_before_set_parent) {
            if (!opts.reaper_parent_shape) ShowWindow(parent, SW_SHOW);
            ShowWindow(frame, SW_SHOW);
            UpdateWindow(frame);
        }

        t0 = now_ms();
        if (!gui->show(plugin)) {
            std::fprintf(stderr, "gui.show failed\n");
            DestroyWindow(parent);
            DestroyWindow(frame);
            gui->destroy(plugin);
            plugin->destroy(plugin);
            return 1;
        }
        log_line("[gui.show] %.1f ms\n", now_ms() - t0);

        if (opts.promote_parent_after_show && parent) {
            if (opts.parent_promote_delay_ms > 0) {
                const double deadline = now_ms() + opts.parent_promote_delay_ms;
                while (now_ms() < deadline) {
                    pump_messages();
                    run_main_thread_callback(plugin, host_state);
                    Sleep(1);
                }
            }
            const LONG_PTR style = GetWindowLongPtrA(parent, GWL_STYLE);
            SetWindowLongPtrA(parent, GWL_STYLE, style | WS_VISIBLE);
            ShowWindow(parent, SW_SHOW);
            UpdateWindow(parent);
            log_line("[parent.promote] hwnd=%p style=0x%llx delay_ms=%d\n",
                     parent,
                     static_cast<long long>(GetWindowLongPtrA(parent, GWL_STYLE)),
                     opts.parent_promote_delay_ms);
        }

        pump_callbacks_for(plugin, host_state, opts.ui_timeout_ms);
    }

    if (!opts.activate_before_ui && opts.activate_delay_ms > 0) {
        log_line("[activate.delay] sleeping %d ms before activate\n", opts.activate_delay_ms);
        const double deadline = now_ms() + opts.activate_delay_ms;
        while (now_ms() < deadline) {
            pump_messages();
            run_main_thread_callback(plugin, host_state);
            Sleep(10);
        }
    }

    if (!activated && !start_audio()) {
        if (activated) {
            plugin->deactivate(plugin);
        }
        if (gui && opts.open_ui) gui->destroy(plugin);
        plugin->destroy(plugin);
        entry->deinit();
        return 1;
    }

    float peak = 0.0f;
    if (opts.run_transport) {
        peak = process_audio(plugin, opts, host_state, opts.process_blocks);
        log_line("[transport] peak=%.6f\n", peak);
    } else {
        pump_callbacks_for(plugin, host_state, opts.ui_timeout_ms);
    }

    if (opts.restart_processing_after_ui && processing) {
        t0 = now_ms();
        plugin->stop_processing(plugin);
        log_line("[stop_processing.restart] %.1f ms\n", now_ms() - t0);
        processing = false;

        t0 = now_ms();
        plugin->get_extension(plugin, "clap.render");
        log_line("[get_extension.render] %.1f ms\n", now_ms() - t0);

        t0 = now_ms();
        plugin->reset(plugin);
        log_line("[reset] %.1f ms\n", now_ms() - t0);

        t0 = now_ms();
        if (!plugin->start_processing(plugin)) {
            std::fprintf(stderr, "restart start_processing failed\n");
        } else {
            processing = true;
            log_line("[start_processing.restart] %.1f ms\n", now_ms() - t0);
            if (opts.restart_process_blocks > 0) {
                const float restart_peak = process_audio(plugin, opts, host_state, opts.restart_process_blocks);
                log_line("[transport.restart] peak=%.6f\n", restart_peak);
                peak = (std::max)(peak, restart_peak);
            }
        }
    }

    if (gui && opts.open_ui) {
        t0 = now_ms();
        gui->hide(plugin);
        log_line("[gui.hide] %.1f ms\n", now_ms() - t0);
    }

    for (int i = 0; i < opts.state_saves_after_ui; ++i) {
        save_state_snapshot(plugin, state, opts.state_saves_before_ui + i + 1);
    }

    if (processing) {
        t0 = now_ms();
        plugin->stop_processing(plugin);
        log_line("[stop_processing] %.1f ms\n", now_ms() - t0);
    }

    if (activated) {
        t0 = now_ms();
        plugin->deactivate(plugin);
        log_line("[deactivate] %.1f ms\n", now_ms() - t0);
    }

    if (gui && opts.open_ui) {
        t0 = now_ms();
        gui->destroy(plugin);
        log_line("[gui.destroy] %.1f ms\n", now_ms() - t0);
    }

    t0 = now_ms();
    plugin->destroy(plugin);
    log_line("[plugin.destroy] %.1f ms\n", now_ms() - t0);

    t0 = now_ms();
    entry->deinit();
    log_line("[entry.deinit] %.1f ms\n", now_ms() - t0);

    if (parent) DestroyWindow(parent);
    if (frame) DestroyWindow(frame);

    log_line("result=%s peak=%.6f\n",
             (!opts.run_transport || peak > 0.00001f) ? "PASS" : "FAIL",
             peak);
    return (!opts.run_transport || peak > 0.00001f) ? 0 : 2;
}
