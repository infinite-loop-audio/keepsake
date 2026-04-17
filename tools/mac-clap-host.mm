#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/state.h>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <limits>
#include <string>
#include <thread>
#include <vector>

@interface KeepsakeHarnessAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
@public
    std::atomic<bool> *_window_closed;
}
- (instancetype)initWithWindowClosedFlag:(std::atomic<bool> *)window_closed;
@end

@implementation KeepsakeHarnessAppDelegate

- (instancetype)initWithWindowClosedFlag:(std::atomic<bool> *)window_closed {
    self = [super init];
    if (!self) return nil;
    _window_closed = window_closed;
    return self;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

- (void)windowWillClose:(NSNotification *)notification {
    (void)notification;
    if (_window_closed) {
        _window_closed->store(true, std::memory_order_release);
    }
    [NSApp terminate:nil];
}

@end

namespace fs = std::filesystem;

namespace {

struct Options {
    std::string clap_path;
    std::string plugin_id;
    std::string vst_path;
    std::string ui_mode = "auto";
    std::string host_mode = "stable";
    std::string attach_target = "auto";
    bool open_ui = false;
    bool run_transport = false;
    bool script_click_center = false;
    std::string script_text;
    int script_delay_ms = 250;
    int width = 960;
    int height = 640;
    int ui_seconds = 5;
    int telemetry_seconds = 0;
    int sample_rate = 44100;
    int block_size = 512;
    int process_blocks = 96;
    bool periodic_editor_idle = true;
    bool periodic_capture = true;
    int gui_idle_interval_us = 16000;
    int capture_interval_us = 0;
    int capture_burst_frames = 6;
    int capture_burst_interval_us = 33333;
};

struct HostState {
    clap_host_t host = {};
    clap_host_gui_t host_gui = {};
    bool callback_requested = false;
    bool restart_requested = false;
    bool process_requested = false;
    NSWindow *window = nil;
    NSView *root = nil;
    NSView *parent = nil;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string host_mode = "stable";
    double ui_opened_ms = 0.0;
    bool delayed_parent_attached = false;
    bool parent_swapped = false;
    std::atomic<bool> window_closed{false};
    double last_main_thread_poll_ms = 0.0;
};

struct BitmapSample {
    uint64_t digest = 0;
    uint64_t nonzero_samples = 0;
    size_t width = 0;
    size_t height = 0;
};

struct MidiEventList {
    clap_input_events_t iface = {};
    std::vector<clap_event_midi_t> events;
    std::vector<const clap_event_header_t *> headers;
};

struct AudioRunState {
    std::atomic<bool> stop{false};
    std::atomic<float> peak{0.0f};
    std::atomic<int> blocks{0};
};

double now_ms() {
    return CACurrentMediaTime() * 1000.0;
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

    char tmpl[] = "/tmp/keepsake-mac-host-XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) return false;

    const fs::path dst = fs::path(dir) / src.filename();
    std::error_code ec;
    fs::create_directory_symlink(src, dst, ec);
    if (ec) return false;
    return setenv("KEEPSAKE_VST2_PATH", dir, 1) == 0;
}

bool parse_args(int argc, char *argv[], Options &opts) {
    if (argc < 3) return false;
    opts.clap_path = argv[1];
    opts.plugin_id = argv[2];
    for (int i = 3; i < argc; ++i) {
        const char *arg = argv[i];
        if (std::strcmp(arg, "--vst-path") == 0 && i + 1 < argc) {
            opts.vst_path = argv[++i];
        } else if (std::strcmp(arg, "--ui-mode") == 0 && i + 1 < argc) {
            opts.ui_mode = argv[++i];
        } else if (std::strcmp(arg, "--host-mode") == 0 && i + 1 < argc) {
            opts.host_mode = argv[++i];
        } else if (std::strcmp(arg, "--attach-target") == 0 && i + 1 < argc) {
            opts.attach_target = argv[++i];
        } else if (std::strcmp(arg, "--open-ui") == 0) {
            opts.open_ui = true;
        } else if (std::strcmp(arg, "--run-transport") == 0) {
            opts.run_transport = true;
        } else if (std::strcmp(arg, "--script-click-center") == 0) {
            opts.script_click_center = true;
        } else if (std::strcmp(arg, "--script-text") == 0 && i + 1 < argc) {
            opts.script_text = argv[++i];
        } else if (std::strcmp(arg, "--script-delay-ms") == 0 && i + 1 < argc) {
            opts.script_delay_ms = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--width") == 0 && i + 1 < argc) {
            opts.width = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--height") == 0 && i + 1 < argc) {
            opts.height = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--ui-seconds") == 0 && i + 1 < argc) {
            opts.ui_seconds = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--telemetry-seconds") == 0 && i + 1 < argc) {
            opts.telemetry_seconds = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--sample-rate") == 0 && i + 1 < argc) {
            opts.sample_rate = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--block-size") == 0 && i + 1 < argc) {
            opts.block_size = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--process-blocks") == 0 && i + 1 < argc) {
            opts.process_blocks = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--periodic-editor-idle") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            opts.periodic_editor_idle =
                std::strcmp(value, "0") != 0 &&
                std::strcmp(value, "off") != 0 &&
                std::strcmp(value, "false") != 0;
        } else if (std::strcmp(arg, "--periodic-capture") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            opts.periodic_capture =
                std::strcmp(value, "0") != 0 &&
                std::strcmp(value, "off") != 0 &&
                std::strcmp(value, "false") != 0;
        } else if (std::strcmp(arg, "--gui-idle-us") == 0 && i + 1 < argc) {
            opts.gui_idle_interval_us = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--capture-interval-us") == 0 && i + 1 < argc) {
            opts.capture_interval_us = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--capture-burst-frames") == 0 && i + 1 < argc) {
            opts.capture_burst_frames = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--capture-burst-interval-us") == 0 && i + 1 < argc) {
            opts.capture_burst_interval_us = std::atoi(argv[++i]);
        } else {
            return false;
        }
    }
    return true;
}

void pump_app_events() {
    @autoreleasepool {
        for (;;) {
            NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                               untilDate:[NSDate dateWithTimeIntervalSinceNow:0]
                                                  inMode:NSDefaultRunLoopMode
                                                 dequeue:YES];
            if (!event) break;
            [NSApp sendEvent:event];
        }
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

void init_midi_list(MidiEventList &list) {
    list.iface.ctx = &list;
    list.iface.size = midi_size;
    list.iface.get = midi_get;
}

void midi_clear(MidiEventList &list) {
    list.events.clear();
    list.headers.clear();
}

void midi_add_note(MidiEventList &list, bool note_on, uint8_t key, uint8_t velocity) {
    clap_event_midi_t event = {};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_MIDI;
    event.data[0] = static_cast<uint8_t>((note_on ? 0x90 : 0x80) | 0);
    event.data[1] = key;
    event.data[2] = velocity;
    list.events.push_back(event);
    list.headers.push_back(&list.events.back().header);
}

const void *host_get_extension(const clap_host_t *host, const char *id) {
    auto *state = static_cast<HostState *>(host->host_data);
    if (id && std::strcmp(id, CLAP_EXT_GUI) == 0) {
        return &state->host_gui;
    }
    return nullptr;
}

void host_request_restart(const clap_host_t *host) {
    auto *state = static_cast<HostState *>(host->host_data);
    state->restart_requested = true;
}

void host_request_process(const clap_host_t *host) {
    auto *state = static_cast<HostState *>(host->host_data);
    state->process_requested = true;
}

void host_request_callback(const clap_host_t *host) {
    auto *state = static_cast<HostState *>(host->host_data);
    state->callback_requested = true;
}

void host_gui_resize_hints_changed(const clap_host_t *) {}

bool host_gui_request_resize(const clap_host_t *host, uint32_t width, uint32_t height) {
    auto *state = static_cast<HostState *>(host->host_data);
    state->width = width;
    state->height = height;
    if (state->parent) {
        [state->parent setFrame:NSMakeRect(0, 0, width, height)];
    }
    if (state->window) {
        NSRect frame = [state->window frameRectForContentRect:NSMakeRect(0, 0, width, height)];
        [state->window setContentSize:frame.size];
    }
    log_line("[host.request_resize] %ux%u\n", width, height);
    return true;
}

bool host_gui_request_show(const clap_host_t *host) {
    auto *state = static_cast<HostState *>(host->host_data);
    if (state->window) [state->window makeKeyAndOrderFront:nil];
    return true;
}

bool host_gui_request_hide(const clap_host_t *host) {
    auto *state = static_cast<HostState *>(host->host_data);
    if (state->window) [state->window orderOut:nil];
    return true;
}

void host_gui_closed(const clap_host_t *host, bool was_destroyed) {
    auto *state = static_cast<HostState *>(host->host_data);
    log_line("[host.closed] destroyed=%d\n", was_destroyed ? 1 : 0);
    if (!state) return;
    state->window_closed.store(true, std::memory_order_release);
    if (state->window) {
        [state->window orderOut:nil];
    }
    [NSApp terminate:nil];
}

void init_host(HostState &state) {
    state.host.clap_version = CLAP_VERSION;
    state.host.host_data = &state;
    state.host.name = "keepsake-mac-clap-host";
    state.host.vendor = "Infinite Loop Audio";
    state.host.url = "";
    state.host.version = "1.0";
    state.host.get_extension = host_get_extension;
    state.host.request_restart = host_request_restart;
    state.host.request_process = host_request_process;
    state.host.request_callback = host_request_callback;

    state.host_gui.resize_hints_changed = host_gui_resize_hints_changed;
    state.host_gui.request_resize = host_gui_request_resize;
    state.host_gui.request_show = host_gui_request_show;
    state.host_gui.request_hide = host_gui_request_hide;
    state.host_gui.closed = host_gui_closed;
}

NSView *make_host_parent(uint32_t width, uint32_t height) {
    NSView *view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    return view;
}

void log_host_tree(HostState &host, const char *phase) {
    log_line("[host.%s] root=%p content=%p parent=%p window=%p root_subviews=%lu parent_window=%p parent_bounds=%.1fx%.1f parent_subviews=%lu\n",
             phase,
             host.root,
             host.window ? host.window.contentView : nil,
             host.parent,
             host.window,
             host.root ? static_cast<unsigned long>(host.root.subviews.count) : 0ul,
             host.parent ? host.parent.window : nil,
             host.parent ? NSWidth(host.parent.bounds) : 0.0,
             host.parent ? NSHeight(host.parent.bounds) : 0.0,
             host.parent ? static_cast<unsigned long>(host.parent.subviews.count) : 0ul);
}

NSView *window_search_root(HostState &host) {
    if (!host.window) return nil;
    NSView *content = host.window.contentView;
    if (!content) return nil;
    return content.superview ? content.superview : content;
}

NSView *find_embed_view(NSView *view);

void log_view_tree(NSView *view, const char *phase, int depth, int max_depth) {
    if (!view || depth > max_depth) return;
    NSString *indent = [@"" stringByPaddingToLength:(NSUInteger)(depth * 2)
                                         withString:@" "
                                    startingAtIndex:0];
    log_line("[host.%s.tree] %s%s view=%p super=%p hidden=%d bounds=%.1fx%.1f subviews=%lu\n",
             phase,
             indent.UTF8String,
             NSStringFromClass([view class]).UTF8String,
             view,
             view.superview,
             view.hidden ? 1 : 0,
             NSWidth(view.bounds),
             NSHeight(view.bounds),
             static_cast<unsigned long>(view.subviews.count));
    for (NSView *child in view.subviews) {
        log_view_tree(child, phase, depth + 1, max_depth);
    }
}

BitmapSample sample_bitmap(NSBitmapImageRep *bitmap) {
    BitmapSample sample = {};
    if (!bitmap) return sample;
    const NSInteger width = bitmap.pixelsWide;
    const NSInteger height = bitmap.pixelsHigh;
    if (width <= 0 || height <= 0) return sample;

    const unsigned char *data = bitmap.bitmapData;
    const NSInteger bytes_per_row = bitmap.bytesPerRow;
    const NSInteger bytes_per_pixel = bitmap.bitsPerPixel / 8;
    if (!data || bytes_per_row <= 0 || bytes_per_pixel < 4) return sample;

    sample.width = static_cast<size_t>(width);
    sample.height = static_cast<size_t>(height);
    sample.digest = 1469598103934665603ull;

    constexpr size_t sample_cols = 16;
    constexpr size_t sample_rows = 16;
    for (size_t row_idx = 0; row_idx < sample_rows; ++row_idx) {
        const size_t y = (row_idx * (sample.height - 1)) /
                         std::max<size_t>(1, sample_rows - 1);
        const unsigned char *row = data + (y * static_cast<size_t>(bytes_per_row));
        for (size_t col_idx = 0; col_idx < sample_cols; ++col_idx) {
            const size_t x = (col_idx * (sample.width - 1)) /
                             std::max<size_t>(1, sample_cols - 1);
            const unsigned char *pixel = row + (x * static_cast<size_t>(bytes_per_pixel));
            const uint32_t packed =
                (static_cast<uint32_t>(pixel[0]) << 0) |
                (static_cast<uint32_t>(pixel[1]) << 8) |
                (static_cast<uint32_t>(pixel[2]) << 16) |
                (static_cast<uint32_t>(pixel[3]) << 24);
            if (pixel[0] || pixel[1] || pixel[2] || pixel[3]) {
                sample.nonzero_samples += 1;
            }
            sample.digest ^= static_cast<uint64_t>(packed);
            sample.digest *= 1099511628211ull;
        }
    }
    return sample;
}

BitmapSample sample_view_pixels(NSView *view) {
    if (!view || NSIsEmptyRect(view.bounds)) return {};
    const NSRect bounds = view.bounds;
    NSBitmapImageRep *bitmap = [view bitmapImageRepForCachingDisplayInRect:bounds];
    if (!bitmap) return {};
    [view cacheDisplayInRect:bounds toBitmapImageRep:bitmap];
    return sample_bitmap(bitmap);
}

void log_host_pixels(HostState &host, const char *phase) {
    NSView *root = window_search_root(host);
    NSView *content = host.window ? host.window.contentView : nil;
    NSView *embed = find_embed_view(root ? root : content);
    if (!root && !content && !embed) {
        log_line("[host.%s.pixels] unavailable\n", phase);
        return;
    }

    const BitmapSample root_sample = sample_view_pixels(root);
    const BitmapSample content_sample = sample_view_pixels(content);
    const BitmapSample embed_sample = sample_view_pixels(embed);
    log_line("[host.%s.pixels] root=%zux%zu digest=0x%016llx nonzero=%llu content=%zux%zu digest=0x%016llx nonzero=%llu embed=%zux%zu digest=0x%016llx nonzero=%llu view=%p super=%p\n",
             phase,
             root_sample.width,
             root_sample.height,
             root_sample.digest,
             root_sample.nonzero_samples,
             content_sample.width,
             content_sample.height,
             content_sample.digest,
             content_sample.nonzero_samples,
             embed_sample.width,
             embed_sample.height,
             embed_sample.digest,
             embed_sample.nonzero_samples,
             embed,
             embed ? embed.superview : nil);
}

void update_host_mode(HostState &host) {
    if (!host.window || !host.root || !host.parent || host.ui_opened_ms <= 0.0) return;
    const double elapsed_ms = now_ms() - host.ui_opened_ms;

    if (host.host_mode == "delayed-parent" && !host.delayed_parent_attached && elapsed_ms >= 150.0) {
        [host.root addSubview:host.parent];
        host.delayed_parent_attached = true;
        log_host_tree(host, "delayed-attach");
        log_view_tree(window_search_root(host), "delayed-attach", 0, 3);
        return;
    }

    if (host.host_mode == "swap-parent" && !host.parent_swapped && elapsed_ms >= 150.0) {
        NSView *new_parent = make_host_parent(host.width, host.height);
        [host.parent removeFromSuperviewWithoutNeedingDisplay];
        [host.root addSubview:new_parent];
        host.parent = new_parent;
        host.parent_swapped = true;
        log_host_tree(host, "swap-parent");
        log_view_tree(window_search_root(host), "swap-parent", 0, 3);
        return;
    }
}

void run_callbacks(const clap_plugin_t *plugin, HostState &host) {
    pump_app_events();
    update_host_mode(host);
    const bool should_poll_main_thread =
        !host.window && (now_ms() - host.last_main_thread_poll_ms) >= 50.0;
    if (host.callback_requested || should_poll_main_thread) {
        host.callback_requested = false;
        host.last_main_thread_poll_ms = now_ms();
        plugin->on_main_thread(plugin);
    }
}

NSView *script_target_view(HostState &host) {
    if (!host.parent) return nil;
    NSArray<NSView *> *subviews = host.parent.subviews;
    if (subviews.count > 0) {
        return subviews.lastObject;
    }
    return host.parent;
}

NSView *find_embed_view(NSView *view) {
    if (!view) return nil;
    if ([view respondsToSelector:@selector(keepsakeDebugMetrics)]) {
        return view;
    }
    for (NSView *child in view.subviews) {
        NSView *found = find_embed_view(child);
        if (found) return found;
    }
    return nil;
}

void log_embed_metrics(HostState &host, const char *phase) {
    NSView *search_root = nil;
    if (window_search_root(host)) {
        search_root = window_search_root(host);
    } else if (host.root) {
        search_root = host.root;
    } else {
        search_root = host.parent;
    }
    NSView *target = find_embed_view(search_root);
    if (!target || ![target respondsToSelector:@selector(keepsakeDebugMetrics)]) {
        log_line("[embed.%s] metrics unavailable\n", phase);
        log_host_pixels(host, phase);
        log_view_tree(search_root, phase, 0, 3);
        return;
    }
    NSDictionary *metrics = [target performSelector:@selector(keepsakeDebugMetrics)];
    if (![metrics isKindOfClass:[NSDictionary class]]) {
        log_line("[embed.%s] metrics invalid\n", phase);
        return;
    }
    const unsigned long long width =
        [[metrics objectForKey:@"surfaceWidth"] unsignedLongLongValue];
    const unsigned long long height =
        [[metrics objectForKey:@"surfaceHeight"] unsignedLongLongValue];
    const unsigned long long refresh_count =
        [[metrics objectForKey:@"refreshCount"] unsignedLongLongValue];
    const unsigned long long digest =
        [[metrics objectForKey:@"digest"] unsignedLongLongValue];
    const unsigned long long digest_changes =
        [[metrics objectForKey:@"digestChanges"] unsignedLongLongValue];
    const unsigned long long mouse_events =
        [[metrics objectForKey:@"mouseEvents"] unsignedLongLongValue];
    const unsigned long long key_events =
        [[metrics objectForKey:@"keyEvents"] unsignedLongLongValue];
    const unsigned long long draw_count =
        [[metrics objectForKey:@"drawCount"] unsignedLongLongValue];
    const double frame_width = [[metrics objectForKey:@"frameWidth"] doubleValue];
    const double frame_height = [[metrics objectForKey:@"frameHeight"] doubleValue];
    const double bounds_width = [[metrics objectForKey:@"boundsWidth"] doubleValue];
    const double bounds_height = [[metrics objectForKey:@"boundsHeight"] doubleValue];
    const double requested_width = [[metrics objectForKey:@"requestedWidth"] doubleValue];
    const double requested_height = [[metrics objectForKey:@"requestedHeight"] doubleValue];
    const double host_width = [[metrics objectForKey:@"hostWidth"] doubleValue];
    const double host_height = [[metrics objectForKey:@"hostHeight"] doubleValue];
    NSString *draw_backend = [metrics objectForKey:@"drawBackend"];
    const char *draw_backend_cstr =
        [draw_backend isKindOfClass:[NSString class]] ? draw_backend.UTF8String : "?";
    log_line("[embed.%s] surface=%llux%llu frame=%.1fx%.1f bounds=%.1fx%.1f requested=%.1fx%.1f host=%.1fx%.1f refresh=%llu draw=%llu backend=%s digest=0x%016llx changes=%llu mouse=%llu key=%llu\n",
             phase,
             width,
             height,
             frame_width,
             frame_height,
             bounds_width,
             bounds_height,
             requested_width,
             requested_height,
             host_width,
             host_height,
             refresh_count,
             draw_count,
             draw_backend_cstr,
             digest,
             digest_changes,
             mouse_events,
             key_events);
    log_host_pixels(host, phase);
}

void monitor_embed_metrics(const clap_plugin_t *plugin, HostState &host, int duration_ms) {
    const double deadline = now_ms() + duration_ms;
    int sample_index = 0;
    while (now_ms() < deadline) {
        run_callbacks(plugin, host);
        log_embed_metrics(host, sample_index == 0 ? "start" : "tick");
        sample_index += 1;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

void dispatch_scripted_input(const Options &opts, const clap_plugin_t *plugin, HostState &host) {
    if (!host.window) return;

    const double deadline = now_ms() + opts.script_delay_ms;
    while (now_ms() < deadline) {
        run_callbacks(plugin, host);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    NSView *target = script_target_view(host);
    if (!target) return;

    if (opts.script_click_center) {
        log_line("[script.click] center\n");
        if ([target respondsToSelector:@selector(keepsakeScriptClickCenter)]) {
            [target performSelector:@selector(keepsakeScriptClickCenter)];
        }
    }

    if (!opts.script_text.empty()) {
        NSString *text = [NSString stringWithUTF8String:opts.script_text.c_str()];
        log_line("[script.text] %s\n", opts.script_text.c_str());
        if ([target respondsToSelector:@selector(keepsakeScriptTypeText:)]) {
            [target performSelector:@selector(keepsakeScriptTypeText:) withObject:text];
        }
    }

    run_callbacks(plugin, host);
}

float process_audio(const clap_plugin_t *plugin,
                    const Options &opts,
                    HostState &host,
                    int duration_ms = 0) {
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
    const double deadline_ms = duration_ms > 0 ? now_ms() + duration_ms : 0.0;
    const bool use_duration = duration_ms > 0;
    const int max_blocks = use_duration ? std::numeric_limits<int>::max() : opts.process_blocks;
    for (int i = 0; i < max_blocks; ++i) {
        midi_clear(midi);
        if (i == 0) {
            midi_add_note(midi, true, 60, 100);
        } else if ((!use_duration && i == 24) ||
                   (use_duration && now_ms() + 50.0 >= deadline_ms)) {
            midi_add_note(midi, false, 60, 0);
        }

        std::fill(out_l.begin(), out_l.end(), 0.0f);
        std::fill(out_r.begin(), out_r.end(), 0.0f);

        const double t0 = now_ms();
        clap_process_status status = plugin->process(plugin, &process);
        float block_peak = 0.0f;
        for (int s = 0; s < opts.block_size; ++s) {
            block_peak = std::max(block_peak, std::fabs(out_l[static_cast<size_t>(s)]));
            block_peak = std::max(block_peak, std::fabs(out_r[static_cast<size_t>(s)]));
        }
        peak = std::max(peak, block_peak);
        if (i < 4 || block_peak > 0.001f || (i % 128) == 0) {
            log_line("[process %03d] %.1f ms status=%d peak=%.6f\n",
                     i, now_ms() - t0, static_cast<int>(status), block_peak);
        }

        run_callbacks(plugin, host);
        std::this_thread::sleep_for(std::chrono::milliseconds(
            std::max(1, static_cast<int>((1000LL * opts.block_size) / std::max(1, opts.sample_rate)))));

        if (use_duration && now_ms() >= deadline_ms) {
            break;
        }
    }
    return peak;
}

void run_audio_thread(const clap_plugin_t *plugin,
                      const Options &opts,
                      AudioRunState &state) {
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

    bool note_on_sent = false;
    bool note_off_sent = false;
    const auto block_sleep = std::chrono::milliseconds(
        std::max(1, static_cast<int>((1000LL * opts.block_size) / std::max(1, opts.sample_rate))));

    while (!state.stop.load(std::memory_order_acquire)) {
        midi_clear(midi);
        if (!note_on_sent) {
            midi_add_note(midi, true, 60, 100);
            note_on_sent = true;
        }

        std::fill(out_l.begin(), out_l.end(), 0.0f);
        std::fill(out_r.begin(), out_r.end(), 0.0f);

        const double t0 = now_ms();
        const clap_process_status status = plugin->process(plugin, &process);
        float block_peak = 0.0f;
        for (int s = 0; s < opts.block_size; ++s) {
            block_peak = std::max(block_peak, std::fabs(out_l[static_cast<size_t>(s)]));
            block_peak = std::max(block_peak, std::fabs(out_r[static_cast<size_t>(s)]));
        }

        float observed_peak = state.peak.load(std::memory_order_relaxed);
        while (block_peak > observed_peak &&
               !state.peak.compare_exchange_weak(observed_peak, block_peak,
                                                 std::memory_order_release,
                                                 std::memory_order_relaxed)) {
        }

        const int block_index = state.blocks.fetch_add(1, std::memory_order_acq_rel);
        if (block_index < 4 || block_peak > 0.001f || (block_index % 128) == 0) {
            log_line("[process %03d] %.1f ms status=%d peak=%.6f\n",
                     block_index, now_ms() - t0, static_cast<int>(status), block_peak);
        }

        std::this_thread::sleep_for(block_sleep);
    }

    if (note_on_sent && !note_off_sent) {
        midi_clear(midi);
        midi_add_note(midi, false, 60, 0);
        std::fill(out_l.begin(), out_l.end(), 0.0f);
        std::fill(out_r.begin(), out_r.end(), 0.0f);
        plugin->process(plugin, &process);
    }
}

std::string clap_binary_path(const std::string &clap_path) {
    return clap_path + "/Contents/MacOS/keepsake";
}

} // namespace

int main(int argc, char *argv[]) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    Options opts;
    if (!parse_args(argc, argv, opts)) {
        std::fprintf(stderr,
                     "usage: %s <clap-bundle> <plugin-id> [--vst-path PATH] [--ui-mode auto|live|preview|iosurface|floating] [--host-mode stable|delayed-parent|swap-parent] [--attach-target auto|requested-parent|content-view|frame-superview] [--open-ui] [--run-transport] [--script-click-center] [--script-text TEXT] [--script-delay-ms N] [--width N] [--height N] [--ui-seconds N] [--telemetry-seconds N] [--sample-rate N] [--block-size N] [--process-blocks N] [--periodic-editor-idle on|off] [--periodic-capture on|off] [--gui-idle-us N] [--capture-interval-us N] [--capture-burst-frames N] [--capture-burst-interval-us N]\n"
                     "note: preview/iosurface is diagnostic-only on macOS; live is the supported interaction posture.\n",
                     argv[0]);
        return 1;
    }

    if (!configure_scan_override(opts.vst_path)) {
        std::fprintf(stderr, "warning: failed to configure scan override for '%s'\n",
                     opts.vst_path.c_str());
    }
    setenv("KEEPSAKE_MAC_UI_MODE", opts.ui_mode.c_str(), 1);
    setenv("KEEPSAKE_MAC_EMBED_ATTACH_TARGET", opts.attach_target.c_str(), 1);
    setenv("KEEPSAKE_MAC_PERIODIC_EDITOR_IDLE", opts.periodic_editor_idle ? "1" : "0", 1);
    setenv("KEEPSAKE_MAC_PERIODIC_CAPTURE", opts.periodic_capture ? "1" : "0", 1);
    {
        const std::string gui_idle_interval = std::to_string(opts.gui_idle_interval_us);
        setenv("KEEPSAKE_MAC_GUI_IDLE_INTERVAL_US", gui_idle_interval.c_str(), 1);
    }
    {
        const std::string capture_interval = std::to_string(opts.capture_interval_us);
        setenv("KEEPSAKE_MAC_CAPTURE_INTERVAL_US", capture_interval.c_str(), 1);
    }
    {
        const std::string capture_burst_frames = std::to_string(opts.capture_burst_frames);
        setenv("KEEPSAKE_MAC_CAPTURE_BURST_FRAMES", capture_burst_frames.c_str(), 1);
    }
    {
        const std::string capture_burst_interval = std::to_string(opts.capture_burst_interval_us);
        setenv("KEEPSAKE_MAC_CAPTURE_BURST_INTERVAL_US", capture_burst_interval.c_str(), 1);
    }

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    void *lib = dlopen(clap_binary_path(opts.clap_path).c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!lib) {
        std::fprintf(stderr, "failed to load CLAP bundle: %s\n", dlerror());
        return 1;
    }

    auto *entry = reinterpret_cast<const clap_plugin_entry_t *>(dlsym(lib, "clap_entry"));
    if (!entry) {
        std::fprintf(stderr, "no clap_entry\n");
        return 1;
    }

    log_line("=== mac-clap-host ===\n");
    log_line("plugin_id=%s ui_mode=%s host_mode=%s attach_target=%s\n",
             opts.plugin_id.c_str(), opts.ui_mode.c_str(), opts.host_mode.c_str(),
             opts.attach_target.c_str());
    log_line("periodic_editor_idle=%d periodic_capture=%d gui_idle_us=%d capture_interval_us=%d burst_frames=%d burst_interval_us=%d\n",
             opts.periodic_editor_idle ? 1 : 0,
             opts.periodic_capture ? 1 : 0,
             opts.gui_idle_interval_us,
             opts.capture_interval_us,
             opts.capture_burst_frames,
             opts.capture_burst_interval_us);

    if (!entry->init(opts.clap_path.c_str())) {
        std::fprintf(stderr, "entry.init failed\n");
        return 1;
    }

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

    HostState host;
    init_host(host);
    host.host_mode = opts.host_mode;
    KeepsakeHarnessAppDelegate *app_delegate =
        [[KeepsakeHarnessAppDelegate alloc] initWithWindowClosedFlag:&host.window_closed];
    [NSApp setDelegate:app_delegate];

    const clap_plugin_t *plugin = factory->create_plugin(factory, &host.host, desc->id);
    if (!plugin) {
        std::fprintf(stderr, "create_plugin failed\n");
        return 1;
    }
    if (!plugin->init(plugin)) {
        std::fprintf(stderr, "plugin.init failed\n");
        plugin->destroy(plugin);
        return 1;
    }

    auto *gui = static_cast<const clap_plugin_gui_t *>(plugin->get_extension(plugin, CLAP_EXT_GUI));
    if (!plugin->activate(plugin, static_cast<double>(opts.sample_rate), 32,
                          static_cast<uint32_t>(opts.block_size))) {
        std::fprintf(stderr, "activate failed\n");
        plugin->destroy(plugin);
        return 1;
    }
    if (!plugin->start_processing(plugin)) {
        std::fprintf(stderr, "start_processing failed\n");
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
        return 1;
    }

    uint32_t ui_w = static_cast<uint32_t>(opts.width);
    uint32_t ui_h = static_cast<uint32_t>(opts.height);

    if (opts.open_ui) {
        if (!gui) {
            std::fprintf(stderr, "plugin has no GUI extension\n");
            plugin->stop_processing(plugin);
            plugin->deactivate(plugin);
            plugin->destroy(plugin);
            return 1;
        }

        const bool request_floating =
            opts.ui_mode == "live" ||
            opts.ui_mode == "floating" ||
            opts.ui_mode == "auto";

        if (!gui->create(plugin, CLAP_WINDOW_API_COCOA, request_floating)) {
            std::fprintf(stderr, "gui.create failed\n");
            plugin->stop_processing(plugin);
            plugin->deactivate(plugin);
            plugin->destroy(plugin);
            return 1;
        }
        gui->set_scale(plugin, 1.0);
        if (gui->get_size(plugin, &ui_w, &ui_h)) {
            log_line("[gui.get_size] %ux%u\n", ui_w, ui_h);
        }

        if (!request_floating) {
            NSRect contentRect = NSMakeRect(200, 200, ui_w, ui_h);
            host.window = [[NSWindow alloc]
                initWithContentRect:contentRect
                          styleMask:(NSWindowStyleMaskTitled |
                                     NSWindowStyleMaskClosable |
                                     NSWindowStyleMaskResizable)
                            backing:NSBackingStoreBuffered
                              defer:NO];
            host.root = make_host_parent(ui_w, ui_h);
            host.parent = make_host_parent(ui_w, ui_h);
            host.width = ui_w;
            host.height = ui_h;
            [host.window setContentView:host.root];
            if (host.host_mode == "stable" || host.host_mode == "swap-parent") {
                [host.root addSubview:host.parent];
                host.delayed_parent_attached = true;
            } else if (host.host_mode == "delayed-parent") {
                host.delayed_parent_attached = false;
            }
            [host.window setTitle:[NSString stringWithUTF8String:opts.plugin_id.c_str()]];
            [host.window setDelegate:app_delegate];
            [host.window makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
            log_host_tree(host, "before-set-parent");
            log_view_tree(window_search_root(host), "before-set-parent", 0, 3);

            clap_window_t window = {};
            window.api = CLAP_WINDOW_API_COCOA;
            window.cocoa = (__bridge void *)host.parent;

            if (!gui->set_parent(plugin, &window)) {
                std::fprintf(stderr, "gui.set_parent failed\n");
                gui->destroy(plugin);
                plugin->stop_processing(plugin);
                plugin->deactivate(plugin);
                plugin->destroy(plugin);
                return 1;
            }
        }
        if (!gui->show(plugin)) {
            std::fprintf(stderr, "gui.show failed\n");
            gui->destroy(plugin);
            plugin->stop_processing(plugin);
            plugin->deactivate(plugin);
            plugin->destroy(plugin);
            return 1;
        }
        host.ui_opened_ms = now_ms();

        log_embed_metrics(host, "after-show");

        if (opts.script_click_center || !opts.script_text.empty()) {
            dispatch_scripted_input(opts, plugin, host);
            log_embed_metrics(host, "after-script");
        }
    }

    float peak = 0.0f;
    if (opts.run_transport && opts.open_ui) {
        AudioRunState audio_state;
        std::thread audio_thread(run_audio_thread, plugin, std::cref(opts), std::ref(audio_state));

        const double deadline = now_ms() + (opts.ui_seconds * 1000.0);
        while (now_ms() < deadline &&
               !host.window_closed.load(std::memory_order_acquire)) {
            run_callbacks(plugin, host);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        audio_state.stop.store(true, std::memory_order_release);
        audio_thread.join();
        peak = audio_state.peak.load(std::memory_order_acquire);
    } else if (opts.run_transport) {
        peak = process_audio(plugin, opts, host, 0);
    } else if (opts.open_ui) {
        const double deadline = now_ms() + (opts.ui_seconds * 1000.0);
        while (now_ms() < deadline &&
               !host.window_closed.load(std::memory_order_acquire)) {
            run_callbacks(plugin, host);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    if (opts.open_ui && opts.telemetry_seconds > 0) {
        monitor_embed_metrics(plugin, host, opts.telemetry_seconds * 1000);
    }

    if (gui && opts.open_ui) {
        gui->hide(plugin);
        gui->destroy(plugin);
    }

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    [app_delegate release];

    log_line("result=%s peak=%.6f\n",
             (!opts.run_transport || peak > 0.00001f) ? "PASS" : "FAIL",
             peak);
    return (!opts.run_transport || peak > 0.00001f) ? 0 : 2;
}
