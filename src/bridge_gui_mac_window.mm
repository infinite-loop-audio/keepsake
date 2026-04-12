//
// Bridge GUI — macOS windowed editor hosting.
//

#import "bridge_gui_mac_internal.h"

#include <atomic>
#include <thread>
#include <unistd.h>

static void style_parentless_plugin_window(NSWindow *window,
                                           const EditorHeaderInfo &header) {
    if (!window) return;

    NSString *title = [NSString stringWithFormat:@"Keepsake — %s",
                       header.plugin_name.c_str()];
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

    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

static bool open_parentless_editor(BridgeLoader *loader,
                                   const EditorHeaderInfo &header) {
    NSArray<NSWindow *> *windows_before = [[NSApp windows] copy];
    bool open_ok = loader->open_editor(nullptr);
    if (!open_ok) {
        fprintf(stderr, "bridge: loader->open_editor() failed\n");
        g_window = nil;
        g_header = nil;
        g_editor_container = nil;
        return false;
    }

    NSArray<NSWindow *> *windows_after = [NSApp windows];
    g_parentless_plugin_window = nil;
    for (NSWindow *candidate in windows_after) {
        if ([windows_before containsObject:candidate]) continue;
        g_parentless_plugin_window = candidate;
        break;
    }

    if (g_parentless_plugin_window) {
        style_parentless_plugin_window(g_parentless_plugin_window, header);
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

    NSString *title = [NSString stringWithFormat:@"Keepsake — %s",
                       header.plugin_name.c_str()];
    [g_window setTitle:title];
    [g_window setReleasedWhenClosed:NO];
    [g_window setLevel:NSFloatingWindowLevel];

    NSView *content = gui_mac_make_content_view(w, h);
    [g_window setContentView:content];

    g_editor_container = gui_mac_make_editor_container(w, h);
    [content addSubview:g_editor_container];

    g_header = gui_mac_make_header_view(w, header);
    [content addSubview:g_header];

    [g_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [g_window displayIfNeeded];
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

    for (int i = 0; i < 8; i++) {
        loader->editor_idle();
        gui_pump_pending_events([NSDate dateWithTimeIntervalSinceNow:0.0]);
        [g_window displayIfNeeded];
        usleep(16000);
    }

    g_editor_open = true;
    return true;
}

bool gui_open_windowed_editor(BridgeLoader *loader, const EditorHeaderInfo &header) {
    const bool use_parentless_open =
        header.plugin_name == "Ample Percussion Cloudrum";
    if (use_parentless_open) return open_parentless_editor(loader, header);
    return open_floating_editor(loader, header);
}

void gui_close_window_state() {
    if (g_frame_change_observer) {
        [[NSNotificationCenter defaultCenter] removeObserver:g_frame_change_observer];
        g_frame_change_observer = nil;
    }
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
