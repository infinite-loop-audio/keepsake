//
// Bridge GUI — macOS windowed editor hosting.
//

#import "bridge_gui_mac_internal.h"

#include "debug_log.h"

#include <atomic>
#include <cstdlib>
#include <thread>
#include <unistd.h>

@interface KeepsakeMacWindowCloseHandler : NSObject
- (void)handleWindowClose:(id)sender;
@end

static id g_parentless_window_will_close_observer = nil;
static id g_parentless_window_did_resize_observer = nil;
static id g_parentless_window_did_move_observer = nil;
static KeepsakeMacWindowCloseHandler *g_window_close_handler = nil;

@implementation KeepsakeMacWindowCloseHandler
- (void)handleWindowClose:(id)__unused sender {
    keepsake_debug_log("bridge/mac: titlebar close requested parentless=%d floating=%d\n",
                       g_parentless_plugin_window ? 1 : 0,
                       g_window ? 1 : 0);
    gui_close_editor(g_active_loader);
}
@end

static KeepsakeMacWindowCloseHandler *window_close_handler() {
    if (!g_window_close_handler) {
        g_window_close_handler = [KeepsakeMacWindowCloseHandler new];
    }
    return g_window_close_handler;
}

static void clear_parentless_window_observers() {
    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    if (g_parentless_window_will_close_observer) {
        [center removeObserver:g_parentless_window_will_close_observer];
        g_parentless_window_will_close_observer = nil;
    }
    if (g_parentless_window_did_resize_observer) {
        [center removeObserver:g_parentless_window_did_resize_observer];
        g_parentless_window_did_resize_observer = nil;
    }
    if (g_parentless_window_did_move_observer) {
        [center removeObserver:g_parentless_window_did_move_observer];
        g_parentless_window_did_move_observer = nil;
    }
}

static void clamp_parentless_plugin_window_to_visible_screen(NSWindow *window,
                                                             const char *phase) {
    if (!window) return;
    NSScreen *screen = [window screen] ?: [NSScreen mainScreen];
    if (!screen) return;

    const NSRect visible = [screen visibleFrame];
    NSRect frame = [window frame];
    const NSRect original = frame;

    const CGFloat max_x = NSMaxX(visible) - frame.size.width;
    if (frame.origin.x < visible.origin.x) frame.origin.x = visible.origin.x;
    if (frame.origin.x > max_x) frame.origin.x = max_x;

    const CGFloat max_y = NSMaxY(visible) - frame.size.height;
    if (frame.origin.y > max_y) frame.origin.y = max_y;

    if (!NSEqualRects(frame, original)) {
        [window setFrameOrigin:frame.origin];
        keepsake_debug_log("bridge/mac: parentless window clamped phase=%s old=%.0fx%.0f@%.0f,%.0f new=%.0fx%.0f@%.0f,%.0f\n",
                           phase,
                           original.size.width, original.size.height,
                           original.origin.x, original.origin.y,
                           frame.size.width, frame.size.height,
                           frame.origin.x, frame.origin.y);
    }
}

static void layout_parentless_wrapped_window(NSWindow *window) {
    if (!window || !g_editor_container || !g_header) return;
    NSView *content = [window contentView];
    if (!content) return;
    const CGFloat width = NSWidth(content.bounds);
    const CGFloat height = NSHeight(content.bounds);
    const CGFloat editor_height = std::max<CGFloat>(0.0, height - HEADER_HEIGHT);
    [g_header setFrame:NSMakeRect(0, 0, width, HEADER_HEIGHT)];
    [g_editor_container setFrame:NSMakeRect(0, HEADER_HEIGHT, width, editor_height)];
    [g_header setNeedsDisplay:YES];
}

static void install_parentless_wrapped_content(NSWindow *window,
                                               const EditorHeaderInfo &header) {
    if (!window) return;
    NSView *plugin_content = [window contentView];
    if (!plugin_content) return;
    if (g_editor_container == plugin_content && g_header && [g_header superview] == [window contentView]) {
        layout_parentless_wrapped_window(window);
        return;
    }

    const NSRect plugin_frame = [plugin_content frame];
    const CGFloat width = NSWidth(plugin_frame);
    const CGFloat height = NSHeight(plugin_frame);

    NSView *wrapper = gui_mac_make_content_view(static_cast<int>(width),
                                                static_cast<int>(height));
    g_editor_container = plugin_content;
    g_header = gui_mac_make_header_view(static_cast<int>(width), header);

    [g_editor_container removeFromSuperviewWithoutNeedingDisplay];
    [wrapper addSubview:g_editor_container];
    [wrapper addSubview:g_header];
    [window setContentView:wrapper];

    NSRect frame = [window frame];
    NSRect content_rect = [window contentRectForFrameRect:frame];
    const CGFloat desired_content_height = height + HEADER_HEIGHT;
    const CGFloat delta = desired_content_height - NSHeight(content_rect);
    if (std::fabs(delta) > 0.5) {
        frame.size.height += delta;
        frame.origin.y -= delta;
        [window setFrame:frame display:YES];
    }

    layout_parentless_wrapped_window(window);
}

static NSInteger score_parentless_candidate_window(NSWindow *window) {
    if (!window) return NSIntegerMin;
    const NSRect frame = [window frame];
    NSInteger score = static_cast<NSInteger>(frame.size.width * frame.size.height);
    if ([window isVisible]) score += 100000000;
    if ([window isMiniaturized]) score -= 100000000;
    if ([window canBecomeKeyWindow]) score += 1000000;
    if ([window isKeyWindow]) score += 500000;
    if ([window isMainWindow]) score += 250000;
    if ([window title] && [[window title] length] > 0) score += 10000;
    if ([window level] == NSNormalWindowLevel) score += 1000;
    return score;
}

static void log_parentless_candidate_window(NSWindow *window,
                                            const char *phase,
                                            NSInteger score) {
    if (!window) return;
    NSString *title = [window title] ?: @"";
    const char *title_utf8 = [title UTF8String] ? [title UTF8String] : "";
    const NSRect frame = [window frame];
    keepsake_debug_log(
        "bridge/mac: parentless candidate phase=%s window=%p class=%s title='%s' "
        "visible=%d key=%d main=%d level=%ld frame=%.0fx%.0f@%.0f,%.0f score=%ld\n",
        phase,
        window,
        object_getClassName(window),
        title_utf8,
        [window isVisible] ? 1 : 0,
        [window isKeyWindow] ? 1 : 0,
        [window isMainWindow] ? 1 : 0,
        static_cast<long>([window level]),
        frame.size.width,
        frame.size.height,
        frame.origin.x,
        frame.origin.y,
        static_cast<long>(score));
}

static NSWindow *select_best_parentless_plugin_window(NSSet<NSWindow *> *windows_before,
                                                      const char *phase) {
    NSArray<NSWindow *> *windows_after = [NSApp windows];
    NSWindow *best = nil;
    NSInteger best_score = NSIntegerMin;
    for (NSWindow *candidate in windows_after) {
        if ([windows_before containsObject:candidate]) continue;
        const NSInteger score = score_parentless_candidate_window(candidate);
        log_parentless_candidate_window(candidate, phase, score);
        if (!best || score > best_score) {
            best = candidate;
            best_score = score;
        }
    }
    return best;
}

static void install_parentless_window_observers(NSWindow *window) {
    clear_parentless_window_observers();
    if (!window) return;
    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    g_parentless_window_will_close_observer =
        [center addObserverForName:NSWindowWillCloseNotification
                            object:window
                             queue:nil
                        usingBlock:^(__unused NSNotification *note) {
                            keepsake_debug_log("bridge/mac: parentless window will close window=%p title='%s'\n",
                                               window,
                                               [[window title] UTF8String] ? [[window title] UTF8String] : "");
                        }];
    g_parentless_window_did_resize_observer =
        [center addObserverForName:NSWindowDidResizeNotification
                            object:window
                             queue:nil
                        usingBlock:^(__unused NSNotification *note) {
                            clamp_parentless_plugin_window_to_visible_screen(window, "did-resize");
                            layout_parentless_wrapped_window(window);
                        }];
    g_parentless_window_did_move_observer =
        [center addObserverForName:NSWindowDidMoveNotification
                            object:window
                             queue:nil
                        usingBlock:^(__unused NSNotification *note) {
                            clamp_parentless_plugin_window_to_visible_screen(window, "did-move");
                        }];
}

static void install_window_close_button_handler(NSWindow *window) {
    if (!window) return;
    NSButton *close_button = [window standardWindowButton:NSWindowCloseButton];
    if (!close_button) return;
    [close_button setTarget:window_close_handler()];
    [close_button setAction:@selector(handleWindowClose:)];
}

static void style_parentless_plugin_window(NSWindow *window,
                                           const EditorHeaderInfo &header) {
    if (!window) return;

    NSString *plugin_name = [NSString stringWithUTF8String:header.plugin_name.c_str()];
    NSString *format = [NSString stringWithUTF8String:header.format.c_str()];
    NSString *arch = [NSString stringWithUTF8String:header.architecture.c_str()];
    NSString *presentation = [NSString stringWithUTF8String:
        (header.presentation.empty() ? "Editor" : header.presentation.c_str())];
    NSString *title = [NSString stringWithFormat:@"Keepsake %@ — %@ [%@ • %@]",
                       presentation, plugin_name, format, arch];
    [window setTitle:title];

    NSWindowStyleMask mask = [window styleMask];
    mask |= NSWindowStyleMaskTitled;
    mask |= NSWindowStyleMaskClosable;
    mask |= NSWindowStyleMaskMiniaturizable;
    [window setStyleMask:mask];
    [window setReleasedWhenClosed:NO];

    NSScreen *screen = [window screen] ?: [NSScreen mainScreen];
    if (screen) {
        NSRect visible = [screen visibleFrame];
        NSRect frame = [window frame];
        if (frame.size.width > visible.size.width) frame.size.width = visible.size.width;
        if (frame.size.height > visible.size.height) frame.size.height = visible.size.height;
        frame.origin.x = visible.origin.x + (visible.size.width - frame.size.width) / 2.0;
        frame.origin.y = visible.origin.y + (visible.size.height - frame.size.height) / 2.0;
        [window setFrame:frame display:YES];
    } else {
        [window center];
    }
    install_window_close_button_handler(window);
    install_parentless_wrapped_content(window, header);
    clamp_parentless_plugin_window_to_visible_screen(window, "style");

    [NSApp activateIgnoringOtherApps:YES];
    [window makeKeyAndOrderFront:nil];
    [window orderFrontRegardless];
    keepsake_debug_log("bridge/mac: parentless window shown key=%d main=%d visible=%d\n",
                       [window isKeyWindow] ? 1 : 0,
                       [window isMainWindow] ? 1 : 0,
                       [window isVisible] ? 1 : 0);
}

static bool open_parentless_editor(BridgeLoader *loader,
                                   const EditorHeaderInfo &header) {
    NSSet<NSWindow *> *windows_before = [NSSet setWithArray:[NSApp windows]];
    bool open_ok = loader->open_editor(nullptr);
    if (!open_ok) {
        fprintf(stderr, "bridge: loader->open_editor() failed\n");
        g_window = nil;
        g_header = nil;
        g_editor_container = nil;
        return false;
    }

    g_parentless_plugin_window = nil;
    for (int i = 0; i < 24; i++) {
        gui_pump_pending_events([NSDate dateWithTimeIntervalSinceNow:0.0]);
        NSWindow *best = select_best_parentless_plugin_window(windows_before,
                                                              "open-scan");
        if (best) {
            g_parentless_plugin_window = best;
        }
        if (i + 1 < 24) usleep(16000);
    }

    if (g_parentless_plugin_window) {
        install_parentless_window_observers(g_parentless_plugin_window);
        style_parentless_plugin_window(g_parentless_plugin_window, header);
        for (int i = 0; i < 24; i++) {
            gui_pump_pending_events([NSDate dateWithTimeIntervalSinceNow:0.0]);
            if (![g_parentless_plugin_window isVisible]) {
                keepsake_debug_log("bridge/mac: tracked parentless window became hidden, rescanning\n");
                NSWindow *replacement = select_best_parentless_plugin_window(windows_before,
                                                                             "settle-rescan");
                if (replacement && replacement != g_parentless_plugin_window) {
                    keepsake_debug_log("bridge/mac: parentless window switched old=%p new=%p\n",
                                       g_parentless_plugin_window, replacement);
                    g_parentless_plugin_window = replacement;
                    install_parentless_window_observers(g_parentless_plugin_window);
                    style_parentless_plugin_window(g_parentless_plugin_window, header);
                }
            }
            if (i + 1 < 24) usleep(16000);
        }
    } else {
        keepsake_debug_log("bridge/mac: no parentless plugin window discovered after open\n");
    }

    g_active_loader = loader;
    g_editor_open = true;
    return true;
}

static void install_frame_observer() {
    [g_editor_container setPostsFrameChangedNotifications:YES];
    for (NSView *subview in [g_editor_container subviews]) {
        [subview setPostsFrameChangedNotifications:YES];
    }

    g_frame_change_observer =
        [[NSNotificationCenter defaultCenter]
            addObserverForName:NSViewFrameDidChangeNotification
            object:nil
            queue:nil
            usingBlock:^(NSNotification *note) {
                NSView *v = [note object];
                if (!g_editor_open || !g_editor_container) return;
                if ([v superview] != g_editor_container && v != g_editor_container) return;

                NSSize sz = [v frame].size;
                int nw = static_cast<int>(sz.width);
                int nh = static_cast<int>(sz.height);
                if (nw <= 0 || nh <= 0 ||
                    (nw == g_current_width && nh == g_current_height)) {
                    return;
                }

                g_current_width = nw;
                g_current_height = nh;

                NSRect frame = [g_window frame];
                NSRect cr = [g_window contentRectForFrameRect:frame];
                CGFloat dh = (nh + HEADER_HEIGHT) - cr.size.height;
                frame.size.width = nw + (frame.size.width - cr.size.width);
                frame.size.height += dh;
                frame.origin.y -= dh;

                [g_window setFrame:frame display:YES animate:NO];
                [g_header setFrame:NSMakeRect(0, 0, nw, HEADER_HEIGHT)];
                [g_editor_container setFrame:NSMakeRect(0, HEADER_HEIGHT, nw, nh)];
                [g_header setNeedsDisplay:YES];
            }];
}

static void update_window_size_after_open(BridgeLoader *loader, int &w, int &h) {
    int newW = w;
    int newH = h;
    loader->get_editor_rect(newW, newH);
    if (newW != w || newH != h) {
        [g_window setContentSize:NSMakeSize(newW, newH + HEADER_HEIGHT)];
        [g_header setFrame:NSMakeRect(0, 0, newW, HEADER_HEIGHT)];
        [g_editor_container setFrame:NSMakeRect(0, HEADER_HEIGHT, newW, newH)];
        w = newW;
        h = newH;
    }
    if (g_window) {
        NSSize fixed = NSMakeSize(w, h + HEADER_HEIGHT);
        NSRect frame = [g_window frameRectForContentRect:NSMakeRect(0, 0, fixed.width, fixed.height)];
        [g_window setContentMinSize:fixed];
        [g_window setContentMaxSize:fixed];
        [g_window setMinSize:frame.size];
        [g_window setMaxSize:frame.size];
    }
    g_current_width = w;
    g_current_height = h;
}

static bool open_floating_editor(BridgeLoader *loader,
                                 const EditorHeaderInfo &header) {
    int w = DEFAULT_EDITOR_WIDTH;
    int h = DEFAULT_EDITOR_HEIGHT;
    CGFloat totalH = h + HEADER_HEIGHT;
    NSRect frame = NSMakeRect(200, 200, w, totalH);
    g_window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskTitled |
                             NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];

    const char *presentation = header.presentation.empty()
        ? "Editor"
        : header.presentation.c_str();
    NSString *title = [NSString stringWithFormat:@"Keepsake %s — %s",
                       presentation,
                       header.plugin_name.c_str()];
    [g_window setTitle:title];
    [g_window setReleasedWhenClosed:NO];
    [g_window setLevel:NSNormalWindowLevel];
    [g_window setHidesOnDeactivate:NO];
    [g_window setStyleMask:([g_window styleMask] & ~NSWindowStyleMaskResizable)];
    [g_window center];
    install_window_close_button_handler(g_window);

    NSView *content = gui_mac_make_content_view(w, h);
    [g_window setContentView:content];

    g_editor_container = gui_mac_make_editor_container(w, h);
    [content addSubview:g_editor_container];

    g_header = gui_mac_make_header_view(w, header);
    [content addSubview:g_header];

    [NSApp activateIgnoringOtherApps:YES];
    [g_window makeKeyAndOrderFront:nil];
    [g_window orderFrontRegardless];
    [g_window displayIfNeeded];
    keepsake_debug_log("bridge/mac: floating window shown key=%d main=%d visible=%d\n",
                       [g_window isKeyWindow] ? 1 : 0,
                       [g_window isMainWindow] ? 1 : 0,
                       [g_window isVisible] ? 1 : 0);
    gui_pump_pending_events([NSDate dateWithTimeIntervalSinceNow:0.0]);

    void *editor_parent = (__bridge void *)g_editor_container;
    std::atomic<bool> open_done{false};
    std::atomic<bool> open_ok{false};
    std::thread open_thread([&]() {
        bool ok = loader->open_editor(editor_parent);
        open_ok.store(ok);
        open_done.store(true);
    });

    const int open_timeout_iters = EDITOR_OPEN_TIMEOUT_MS / 16;
    for (int i = 0; i < open_timeout_iters && !open_done.load(); i++) {
        gui_pump_pending_events([NSDate dateWithTimeIntervalSinceNow:0.0]);
        [g_window displayIfNeeded];
        usleep(16000);
    }

    if (!open_done.load()) {
        fprintf(stderr, "bridge: loader->open_editor() timed out after %dms\n",
                EDITOR_OPEN_TIMEOUT_MS);
        open_thread.detach();
        return false;
    }

    open_thread.join();
    if (!open_ok.load()) {
        fprintf(stderr, "bridge: loader->open_editor() failed\n");
        [g_window orderOut:nil];
        g_window = nil;
        g_header = nil;
        g_editor_container = nil;
        return false;
    }

    g_active_loader = loader;
    install_frame_observer();
    update_window_size_after_open(loader, w, h);
    [NSApp activateIgnoringOtherApps:YES];
    [g_window makeKeyAndOrderFront:nil];
    [g_window orderFrontRegardless];
    [g_window displayIfNeeded];

    for (int i = 0; i < 8; i++) {
        loader->editor_idle();
        gui_pump_pending_events([NSDate dateWithTimeIntervalSinceNow:0.0]);
        [g_window displayIfNeeded];
        usleep(16000);
    }

    g_editor_open = true;
    return true;
}

static bool gui_mac_should_force_parentless_open(const EditorHeaderInfo &header) {
    const char *env = std::getenv("KEEPSAKE_MAC_PARENTLESS_OPEN");
    if (env && env[0]) {
        if (std::strcmp(env, "1") == 0 ||
            std::strcmp(env, "true") == 0 ||
            std::strcmp(env, "on") == 0 ||
            std::strcmp(env, "always") == 0) {
            return true;
        }
        if (std::strcmp(env, "0") == 0 ||
            std::strcmp(env, "false") == 0 ||
            std::strcmp(env, "off") == 0 ||
            std::strcmp(env, "never") == 0) {
            return false;
        }
    }
    if (header.architecture == "x64") {
        return true;
    }
    return false;
}

bool gui_open_windowed_editor(BridgeLoader *loader, const EditorHeaderInfo &header) {
    const bool use_parentless_open = gui_mac_should_force_parentless_open(header);
    if (use_parentless_open) return open_parentless_editor(loader, header);
    return open_floating_editor(loader, header);
}

void gui_close_window_state() {
    if (g_frame_change_observer) {
        [[NSNotificationCenter defaultCenter] removeObserver:g_frame_change_observer];
        g_frame_change_observer = nil;
    }
    clear_parentless_window_observers();
    if (g_parentless_plugin_window) {
        [g_parentless_plugin_window orderOut:nil];
        g_parentless_plugin_window = nil;
    }
    if (g_window) {
        [g_window orderOut:nil];
        g_window = nil;
    }
    g_header = nil;
    g_editor_container = nil;
}
