//
// Bridge GUI — macOS IOSurface editor hosting.
//

#import "bridge_gui_mac_internal.h"

#import <IOSurface/IOSurface.h>
#import <QuartzCore/QuartzCore.h>

#include "debug_log.h"
#include "ipc.h"
#include <atomic>
#include <mach/mach_time.h>

extern std::atomic<int32_t> s_last_automated_param;
extern std::atomic<uint32_t> s_automate_count;
extern std::atomic<uint32_t> s_begin_edit_count;
extern std::atomic<uint32_t> s_end_edit_count;

static IOSurfaceRef g_surface = nullptr;
static NSView *g_offscreen_view = nil;
static NSWindow *g_offscreen_window = nil;
static NSView *g_mouse_target_view = nil;
static NSResponder *g_key_target = nil;
static BridgeLoader *g_capture_loader = nullptr;
static bool g_iosurface_telemetry_enabled = false;
static uint64_t g_capture_count = 0;
static uint64_t g_input_event_count = 0;
static uint64_t g_capture_time_ns = 0;
static uint64_t g_last_telemetry_log_ns = 0;
static NSUInteger g_last_window_snapshot_count = 0;
static bool g_dumped_view_tree = false;
static NSRect g_capture_rect = {{0, 0}, {0, 0}};
static NSInteger g_mouse_event_number = 1;
static NSInteger g_mouse_click_count = 0;

struct ScopedEditorFrameLock {
    explicit ScopedEditorFrameLock(BridgeLoader *loader)
        : loader(loader) {
        if (this->loader) this->loader->lock_editor_frame();
    }

    ~ScopedEditorFrameLock() {
        if (loader) loader->unlock_editor_frame();
    }

    BridgeLoader *loader;
};

static void gui_log_window_snapshot_if_changed() {
    NSArray<NSWindow *> *windows = [NSApp windows];
    const NSUInteger count = windows.count;
    if (count == g_last_window_snapshot_count) return;
    g_last_window_snapshot_count = count;

    keepsake_debug_log("bridge/mac: iosurface window snapshot count=%lu\n",
                       static_cast<unsigned long>(count));
    for (NSWindow *window in windows) {
        keepsake_debug_log(
            "bridge/mac: window=%p class=%s title='%s' visible=%d key=%d main=%d level=%ld frame=%.1fx%.1f@%.1f,%.1f content=%p\n",
            window,
            NSStringFromClass([window class]).UTF8String,
            window.title.UTF8String ? window.title.UTF8String : "",
            window.isVisible ? 1 : 0,
            window.isKeyWindow ? 1 : 0,
            window.isMainWindow ? 1 : 0,
            static_cast<long>(window.level),
            NSWidth(window.frame),
            NSHeight(window.frame),
            NSMinX(window.frame),
            NSMinY(window.frame),
            window.contentView);
    }
}

static void gui_log_view_tree(NSView *view, int depth, int max_depth) {
    if (!view || depth > max_depth) return;
    CALayer *layer = [view layer];
    keepsake_debug_log(
        "bridge/mac: view depth=%d class=%s view=%p hidden=%d frame=%.1fx%.1f@%.1f,%.1f layer=%p layerClass=%s wantsLayer=%d subviews=%lu\n",
        depth,
        NSStringFromClass([view class]).UTF8String,
        view,
        view.hidden ? 1 : 0,
        NSWidth(view.frame),
        NSHeight(view.frame),
        NSMinX(view.frame),
        NSMinY(view.frame),
        layer,
        layer ? NSStringFromClass([layer class]).UTF8String : "(none)",
        view.wantsLayer ? 1 : 0,
        static_cast<unsigned long>(view.subviews.count));

    for (NSView *child in view.subviews) {
        gui_log_view_tree(child, depth + 1, max_depth);
    }
}

static void gui_log_layer_tree(CALayer *layer, int depth, int max_depth) {
    if (!layer || depth > max_depth) return;
    keepsake_debug_log(
        "bridge/mac: layer depth=%d class=%s layer=%p hidden=%d frame=%.1fx%.1f@%.1f,%.1f sublayers=%lu contents=%p delegate=%p\n",
        depth,
        NSStringFromClass([layer class]).UTF8String,
        layer,
        layer.hidden ? 1 : 0,
        layer.bounds.size.width,
        layer.bounds.size.height,
        layer.frame.origin.x,
        layer.frame.origin.y,
        static_cast<unsigned long>(layer.sublayers.count),
        layer.contents,
        layer.delegate);

    for (CALayer *child in layer.sublayers) {
        gui_log_layer_tree(child, depth + 1, max_depth);
    }
}

static void gui_prepare_layer_tree(CALayer *layer) {
    if (!layer) return;
    [layer layoutIfNeeded];
    [layer displayIfNeeded];
    for (CALayer *child in layer.sublayers) {
        gui_prepare_layer_tree(child);
    }
}

static CALayer *gui_capture_root_layer() {
    if (!g_offscreen_view) return nil;
    CALayer *layer = [g_offscreen_view layer];
    if (!layer) return nil;
    CALayer *presentation = [layer presentationLayer];
    return presentation ? presentation : layer;
}

static void gui_dump_editor_tree_once() {
    if (g_dumped_view_tree || !g_offscreen_view) return;
    g_dumped_view_tree = true;
    keepsake_debug_log("bridge/mac: dumping iosurface editor tree\n");
    gui_log_view_tree(g_offscreen_view, 0, 6);
    gui_log_layer_tree([g_offscreen_view layer], 0, 6);
}

static bool gui_should_composite_window(NSWindow *window) {
    if (!window || window == g_offscreen_window) return false;
    if (!window.contentView) return false;
    if ([window isMiniaturized]) return false;
    if ([window alphaValue] <= 0.0) return false;

    const bool popup_menu_window =
        [window isKindOfClass:NSClassFromString(@"NSPopupMenuWindow")];
    if (popup_menu_window) return true;

    const long level = static_cast<long>(window.level);
    const bool tooltip_like =
        level >= NSPopUpMenuWindowLevel &&
        !window.isVisible &&
        NSHeight(window.frame) < 80.0;
    if (tooltip_like) return false;

    return false;
}

static void gui_composite_auxiliary_windows(CGContextRef ctx) {
    if (!ctx || !g_offscreen_window) return;

    const NSRect main_frame = [g_offscreen_window frame];
    for (NSWindow *window in [NSApp windows]) {
        if (!gui_should_composite_window(window)) continue;

        [window displayIfNeeded];
        [window.contentView displayIfNeeded];

        const NSRect bounds = [window.contentView bounds];
        if (NSIsEmptyRect(bounds)) continue;

        NSBitmapImageRep *bitmap =
            [window.contentView bitmapImageRepForCachingDisplayInRect:bounds];
        if (!bitmap) continue;

        [window.contentView cacheDisplayInRect:bounds toBitmapImageRep:bitmap];
        CGImageRef image = [bitmap CGImage];
        if (!image) continue;

        const NSRect frame = [window frame];
        const CGFloat dest_x = NSMinX(frame) - NSMinX(main_frame);
        const CGFloat dest_y = NSMaxY(main_frame) - NSMaxY(frame);
        const CGRect dest_rect = CGRectMake(dest_x,
                                            dest_y,
                                            CGImageGetWidth(image),
                                            CGImageGetHeight(image));

        CGContextSaveGState(ctx);
        CGContextTranslateCTM(ctx, 0.0, IOSurfaceGetHeight(g_surface));
        CGContextScaleCTM(ctx, 1.0, -1.0);
        const CGRect flipped_rect =
            CGRectMake(dest_rect.origin.x,
                       IOSurfaceGetHeight(g_surface) - dest_rect.origin.y - dest_rect.size.height,
                       dest_rect.size.width,
                       dest_rect.size.height);
        CGContextDrawImage(ctx, flipped_rect, image);
        CGContextRestoreGState(ctx);
    }
}

static IOSurfaceRef gui_create_iosurface(int width, int height) {
    if (width <= 0 || height <= 0) return nullptr;
    NSDictionary *props = @{
        (id)kIOSurfaceWidth: @(width),
        (id)kIOSurfaceHeight: @(height),
        (id)kIOSurfaceBytesPerElement: @4,
        (id)kIOSurfaceBytesPerRow: @(width * 4),
        (id)kIOSurfacePixelFormat: @((uint32_t)'BGRA'),
        (id)kIOSurfaceIsGlobal: @YES,
    };
    return IOSurfaceCreate((__bridge CFDictionaryRef)props);
}

static bool gui_use_visible_iosurface_host() {
    const char *flag = std::getenv("KEEPSAKE_MAC_IOSURFACE_VISIBLE_HOST");
    if (!flag || !flag[0]) return true;
    return std::strcmp(flag, "0") != 0;
}

static bool gui_resize_iosurface_surface(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    IOSurfaceRef surface = gui_create_iosurface(width, height);
    if (!surface) {
        keepsake_debug_log("bridge/mac: failed to resize IOSurface surface to %dx%d\n", width, height);
        return false;
    }
    if (g_surface) {
        CFRelease(g_surface);
    }
    g_surface = surface;
    g_current_width = width;
    g_current_height = height;
    return true;
}

static bool gui_resize_iosurface_host_frame(int width, int height) {
    if (!g_offscreen_window || !g_offscreen_view || width <= 0 || height <= 0) {
        return false;
    }
    [g_offscreen_view setFrame:NSMakeRect(0, 0, width, height)];
    [g_offscreen_window setContentSize:NSMakeSize(width, height)];
    [g_offscreen_window setFrame:NSMakeRect(0, -10000, width, height) display:NO];
    return true;
}

static void gui_accumulate_content_rect(NSView *view, NSView *root, NSRect &union_rect, bool &have_rect) {
    if (!view || view == root || view.hidden) return;

    const NSRect bounds = view.bounds;
    if (!NSIsEmptyRect(bounds)) {
        const NSRect rect_in_root = [root convertRect:bounds fromView:view];
        if (!NSIsEmptyRect(rect_in_root)) {
            union_rect = have_rect ? NSUnionRect(union_rect, rect_in_root) : rect_in_root;
            have_rect = true;
        }
    }

    for (NSView *child in view.subviews) {
        gui_accumulate_content_rect(child, root, union_rect, have_rect);
    }
}

static NSRect gui_detect_capture_rect() {
    if (!g_offscreen_view) return NSZeroRect;
    NSRect union_rect = NSZeroRect;
    bool have_rect = false;
    for (NSView *child in g_offscreen_view.subviews) {
        gui_accumulate_content_rect(child, g_offscreen_view, union_rect, have_rect);
    }
    if (!have_rect) {
        union_rect = g_offscreen_view.bounds;
    }
    union_rect = NSIntegralRect(union_rect);
    if (NSIsEmptyRect(union_rect)) {
        union_rect = NSIntegralRect(g_offscreen_view.bounds);
    }
    if (NSIsEmptyRect(union_rect)) {
        union_rect = NSMakeRect(0, 0,
                                std::max(1, g_current_width),
                                std::max(1, g_current_height));
    }
    return union_rect;
}

static bool gui_refresh_capture_layout(bool allow_host_resize, bool allow_surface_resize) {
    if (!g_offscreen_view || !g_offscreen_window) return false;

    [g_offscreen_view layoutSubtreeIfNeeded];
    [g_offscreen_view displayIfNeeded];
    if (g_offscreen_window) [g_offscreen_window displayIfNeeded];

    NSRect capture_rect = gui_detect_capture_rect();
    if (NSIsEmptyRect(capture_rect)) return false;

    const int needed_host_width =
        static_cast<int>(std::ceil(std::max(NSWidth(g_offscreen_view.bounds), NSMaxX(capture_rect))));
    const int needed_host_height =
        static_cast<int>(std::ceil(std::max(NSHeight(g_offscreen_view.bounds), NSMaxY(capture_rect))));

    if (allow_host_resize &&
        (needed_host_width > static_cast<int>(NSWidth(g_offscreen_view.bounds)) ||
         needed_host_height > static_cast<int>(NSHeight(g_offscreen_view.bounds)))) {
        gui_resize_iosurface_host_frame(needed_host_width, needed_host_height);
        [g_offscreen_view layoutSubtreeIfNeeded];
        [g_offscreen_view displayIfNeeded];
        capture_rect = gui_detect_capture_rect();
    }

    const int capture_width = std::max(1, static_cast<int>(std::ceil(NSWidth(capture_rect))));
    const int capture_height = std::max(1, static_cast<int>(std::ceil(NSHeight(capture_rect))));
    if (allow_surface_resize &&
        (!g_surface ||
         static_cast<int>(IOSurfaceGetWidth(g_surface)) != capture_width ||
         static_cast<int>(IOSurfaceGetHeight(g_surface)) != capture_height)) {
        if (!gui_resize_iosurface_surface(capture_width, capture_height)) {
            return false;
        }
    }

    g_capture_rect = capture_rect;
    keepsake_debug_log("bridge/mac: capture rect %.1fx%.1f@%.1f,%.1f host=%.1fx%.1f surface=%zux%zu\n",
                       NSWidth(g_capture_rect),
                       NSHeight(g_capture_rect),
                       NSMinX(g_capture_rect),
                       NSMinY(g_capture_rect),
                       NSWidth(g_offscreen_view.bounds),
                       NSHeight(g_offscreen_view.bounds),
                       g_surface ? IOSurfaceGetWidth(g_surface) : 0,
                       g_surface ? IOSurfaceGetHeight(g_surface) : 0);
    return true;
}

bool gui_resize_editor_iosurface(int width, int height, uint32_t &surface_id) {
    if (!g_iosurface_mode) return false;
    gui_resize_iosurface_host_frame(width, height);
    if (!gui_refresh_capture_layout(true, true)) return false;
    surface_id = g_surface ? IOSurfaceGetID(g_surface) : 0;
    gui_capture_iosurface_if_needed();
    return surface_id != 0;
}

bool gui_get_editor_iosurface_size(int &width, int &height) {
    if (!g_iosurface_mode) return false;
    gui_refresh_capture_layout(true, true);
    if (!g_surface) return false;
    width = static_cast<int>(IOSurfaceGetWidth(g_surface));
    height = static_cast<int>(IOSurfaceGetHeight(g_surface));
    return width > 0 && height > 0;
}

static uint64_t gui_now_ns() {
    static mach_timebase_info_data_t timebase = {0, 0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    const uint64_t t = mach_absolute_time();
    return (t * timebase.numer) / timebase.denom;
}

static void gui_log_iosurface_telemetry_if_needed() {
    if (!g_iosurface_telemetry_enabled) return;
    const uint64_t now_ns = gui_now_ns();
    if (g_last_telemetry_log_ns == 0) {
        g_last_telemetry_log_ns = now_ns;
        return;
    }
    const uint64_t elapsed_ns = now_ns - g_last_telemetry_log_ns;
    if (elapsed_ns < 1000000000ull) return;
    const double elapsed_s = static_cast<double>(elapsed_ns) / 1.0e9;
    const double fps = elapsed_s > 0.0 ? static_cast<double>(g_capture_count) / elapsed_s : 0.0;
    const double avg_capture_ms = g_capture_count > 0
        ? (static_cast<double>(g_capture_time_ns) / static_cast<double>(g_capture_count)) / 1.0e6
        : 0.0;
    keepsake_debug_log("bridge/mac: iosurface telemetry fps=%.1f avg_capture_ms=%.3f input_events=%llu size=%zux%zu\n",
                       fps,
                       avg_capture_ms,
                       static_cast<unsigned long long>(g_input_event_count),
                       g_surface ? IOSurfaceGetWidth(g_surface) : 0,
                       g_surface ? IOSurfaceGetHeight(g_surface) : 0);
    g_capture_count = 0;
    g_input_event_count = 0;
    g_capture_time_ns = 0;
    g_last_telemetry_log_ns = now_ns;
}

@interface KeepsakeIOSurfaceHostView : NSView
@end

@implementation KeepsakeIOSurfaceHostView
- (BOOL)isFlipped { return YES; }
@end

static NSView *gui_mouse_fallback_target() {
    if (g_offscreen_view.subviews.count > 0) {
        return g_offscreen_view.subviews.firstObject;
    }
    return g_offscreen_view;
}

static NSView *gui_plugin_content_root() {
    NSView *root = gui_mouse_fallback_target();
    return root ? root : g_offscreen_view;
}

static NSView *gui_pick_mouse_target(NSPoint point) {
    NSView *target = nil;
    NSView *content_root = gui_plugin_content_root();
    if (g_mouse_target_view) {
        target = g_mouse_target_view;
    } else if (content_root) {
        const NSPoint local_point = [content_root convertPoint:point fromView:g_offscreen_view];
        target = [content_root hitTest:local_point];
    }
    if (!target) {
        target = content_root;
    }
    return target;
}

static NSView *gui_resolve_mouse_dispatch_target(NSView *target_view, SEL selector) {
    NSView *cursor = target_view;
    while (cursor) {
        if ([cursor respondsToSelector:selector]) return cursor;
        if (cursor == g_offscreen_view) break;
        cursor = cursor.superview;
    }
    return target_view;
}

static void gui_prepare_window_for_input(NSResponder *target) {
    if (!g_offscreen_window) return;
    [g_offscreen_window makeKeyWindow];
    if (target) {
        [g_offscreen_window makeFirstResponder:target];
        g_key_target = target;
    }
}

static void gui_log_mouse_trace(const char *phase,
                                NSEvent *event,
                                NSView *hit_view,
                                NSView *dispatch_view,
                                uint32_t before_begin,
                                uint32_t before_automate,
                                uint32_t before_end) {
    if (!event) return;
    keepsake_debug_log(
        "bridge/mac: mouse %s type=%ld loc=%.1f,%.1f hit=%s(%p) dispatch=%s(%p) edits=%u/%u/%u -> %u/%u/%u last_param=%d\n",
        phase ? phase : "?",
        static_cast<long>(event.type),
        event.locationInWindow.x,
        event.locationInWindow.y,
        hit_view ? NSStringFromClass([hit_view class]).UTF8String : "(nil)",
        hit_view,
        dispatch_view ? NSStringFromClass([dispatch_view class]).UTF8String : "(nil)",
        dispatch_view,
        before_begin,
        before_automate,
        before_end,
        s_begin_edit_count.load(),
        s_automate_count.load(),
        s_end_edit_count.load(),
        s_last_automated_param.load());
}

static CGMouseButton gui_cg_mouse_button(int32_t button) {
    switch (button) {
    case 1: return kCGMouseButtonRight;
    case 2: return kCGMouseButtonCenter;
    default: return kCGMouseButtonLeft;
    }
}

static CGEventType gui_cg_mouse_event_type(const IpcMouseEvent &ev) {
    switch (ev.type) {
    case 0:
        return kCGEventMouseMoved;
    case 1:
        return ev.button == 1 ? kCGEventRightMouseDown
                              : (ev.button == 2 ? kCGEventOtherMouseDown
                                                : kCGEventLeftMouseDown);
    case 2:
        return ev.button == 1 ? kCGEventRightMouseUp
                              : (ev.button == 2 ? kCGEventOtherMouseUp
                                                : kCGEventLeftMouseUp);
    case 3:
        return ev.button == 1 ? kCGEventRightMouseDragged
                              : (ev.button == 2 ? kCGEventOtherMouseDragged
                                                : kCGEventLeftMouseDragged);
    default:
        return kCGEventNull;
    }
}

static void gui_dispatch_mouse_event(NSView *target_view, NSEvent *event) {
    if (!target_view || !event) return;
    NSView *dispatch_target = target_view;
    switch (event.type) {
    case NSEventTypeMouseMoved:
        dispatch_target = gui_resolve_mouse_dispatch_target(target_view, @selector(mouseMoved:));
        break;
    case NSEventTypeLeftMouseDown:
        dispatch_target = gui_resolve_mouse_dispatch_target(target_view, @selector(mouseDown:));
        break;
    case NSEventTypeLeftMouseUp:
        dispatch_target = gui_resolve_mouse_dispatch_target(target_view, @selector(mouseUp:));
        break;
    case NSEventTypeLeftMouseDragged:
        dispatch_target = gui_resolve_mouse_dispatch_target(target_view, @selector(mouseDragged:));
        break;
    case NSEventTypeRightMouseDown:
        dispatch_target = gui_resolve_mouse_dispatch_target(target_view, @selector(rightMouseDown:));
        break;
    case NSEventTypeRightMouseUp:
        dispatch_target = gui_resolve_mouse_dispatch_target(target_view, @selector(rightMouseUp:));
        break;
    case NSEventTypeRightMouseDragged:
        dispatch_target = gui_resolve_mouse_dispatch_target(target_view, @selector(rightMouseDragged:));
        break;
    case NSEventTypeOtherMouseDown:
        dispatch_target = gui_resolve_mouse_dispatch_target(target_view, @selector(otherMouseDown:));
        break;
    case NSEventTypeOtherMouseUp:
        dispatch_target = gui_resolve_mouse_dispatch_target(target_view, @selector(otherMouseUp:));
        break;
    case NSEventTypeOtherMouseDragged:
        dispatch_target = gui_resolve_mouse_dispatch_target(target_view, @selector(otherMouseDragged:));
        break;
    case NSEventTypeScrollWheel:
        dispatch_target = gui_resolve_mouse_dispatch_target(target_view, @selector(scrollWheel:));
        break;
    default:
        break;
    }

    const bool trace_mouse =
        event.type != NSEventTypeMouseMoved && event.type != NSEventTypeScrollWheel;
    const uint32_t before_begin = trace_mouse ? s_begin_edit_count.load() : 0;
    const uint32_t before_automate = trace_mouse ? s_automate_count.load() : 0;
    const uint32_t before_end = trace_mouse ? s_end_edit_count.load() : 0;
    if (trace_mouse) {
        gui_log_mouse_trace("pre", event, target_view, dispatch_target,
                            before_begin, before_automate, before_end);
    }

    if (g_offscreen_window) {
        gui_prepare_window_for_input(dispatch_target);
        [g_offscreen_window sendEvent:event];
        if (trace_mouse) {
            gui_log_mouse_trace("post", event, target_view, dispatch_target,
                                before_begin, before_automate, before_end);
        }
        return;
    }
    switch (event.type) {
    case NSEventTypeMouseMoved:
        if (dispatch_target) {
            gui_prepare_window_for_input(dispatch_target);
            [dispatch_target mouseMoved:event];
        }
        break;
    case NSEventTypeLeftMouseDown:
        if (dispatch_target) {
            gui_prepare_window_for_input(dispatch_target);
            [dispatch_target mouseDown:event];
        }
        break;
    case NSEventTypeLeftMouseUp:
        if (dispatch_target) {
            gui_prepare_window_for_input(dispatch_target);
            [dispatch_target mouseUp:event];
        }
        break;
    case NSEventTypeLeftMouseDragged:
        if (dispatch_target) {
            gui_prepare_window_for_input(dispatch_target);
            [dispatch_target mouseDragged:event];
        }
        break;
    case NSEventTypeRightMouseDown:
        if (dispatch_target) {
            gui_prepare_window_for_input(dispatch_target);
            [dispatch_target rightMouseDown:event];
        }
        break;
    case NSEventTypeRightMouseUp:
        if (dispatch_target) {
            gui_prepare_window_for_input(dispatch_target);
            [dispatch_target rightMouseUp:event];
        }
        break;
    case NSEventTypeRightMouseDragged:
        if (dispatch_target) {
            gui_prepare_window_for_input(dispatch_target);
            [dispatch_target rightMouseDragged:event];
        }
        break;
    case NSEventTypeOtherMouseDown:
        if (dispatch_target) {
            gui_prepare_window_for_input(dispatch_target);
            [dispatch_target otherMouseDown:event];
        }
        break;
    case NSEventTypeOtherMouseUp:
        if (dispatch_target) {
            gui_prepare_window_for_input(dispatch_target);
            [dispatch_target otherMouseUp:event];
        }
        break;
    case NSEventTypeOtherMouseDragged:
        if (dispatch_target) {
            gui_prepare_window_for_input(dispatch_target);
            [dispatch_target otherMouseDragged:event];
        }
        break;
    case NSEventTypeScrollWheel:
        if (dispatch_target) {
            gui_prepare_window_for_input(dispatch_target);
            [dispatch_target scrollWheel:event];
        }
        break;
    default:
        break;
    }
    if (trace_mouse) {
        gui_log_mouse_trace("post", event, target_view, dispatch_target,
                            before_begin, before_automate, before_end);
    }
}

static void gui_dispatch_key_event(NSEvent *event) {
    if (!event) return;
    if (g_offscreen_window) {
        [g_offscreen_window sendEvent:event];
        return;
    }
    NSResponder *target = g_key_target;
    if (!target && g_offscreen_window) {
        target = [g_offscreen_window firstResponder];
    }
    if (!target) {
        target = gui_mouse_fallback_target();
    }
    if (!target) return;
    gui_prepare_window_for_input(target);
    switch (event.type) {
    case NSEventTypeKeyDown:
        if ([target respondsToSelector:@selector(keyDown:)]) {
            [target keyDown:event];
        }
        break;
    case NSEventTypeKeyUp:
        if ([target respondsToSelector:@selector(keyUp:)]) {
            [target keyUp:event];
        }
        break;
    default:
        break;
    }
}

uint32_t gui_open_editor_iosurface(BridgeLoader *loader, int width, int height) {
    if (!loader || !loader->has_editor()) return 0;
    g_iosurface_telemetry_enabled = getenv("KEEPSAKE_MAC_EMBED_TELEMETRY") != nullptr;
    g_capture_count = 0;
    g_input_event_count = 0;
    g_capture_time_ns = 0;
    g_last_telemetry_log_ns = 0;
    g_last_window_snapshot_count = 0;
    g_dumped_view_tree = false;
    g_mouse_target_view = nil;
    g_key_target = nil;
    g_capture_loader = loader;

    g_surface = gui_create_iosurface(width, height);
    if (!g_surface) {
        fprintf(stderr, "bridge: failed to create IOSurface\n");
        return 0;
    }
    g_current_width = width;
    g_current_height = height;

    NSRect frame = NSMakeRect(0, -10000, width, height);
    if (gui_use_visible_iosurface_host()) {
        NSScreen *screen = [NSScreen mainScreen];
        NSRect visible = screen ? [screen visibleFrame] : NSMakeRect(100, 100, width, height);
        frame.origin.x = NSMinX(visible) + 24.0;
        frame.origin.y = NSMaxY(visible) - height - 24.0;
    }
    NSWindow *window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskBorderless
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [window setReleasedWhenClosed:NO];
    [window setHidesOnDeactivate:NO];
    [window setExcludedFromWindowsMenu:YES];
    if (gui_use_visible_iosurface_host()) {
        [window setOpaque:NO];
        [window setBackgroundColor:[NSColor clearColor]];
        [window setAlphaValue:0.02];
        [window setIgnoresMouseEvents:YES];
        [window setLevel:NSNormalWindowLevel];
    }
    g_offscreen_window = window;

    g_offscreen_view = [[KeepsakeIOSurfaceHostView alloc]
        initWithFrame:NSMakeRect(0, 0, width, height)];
    [g_offscreen_view setWantsLayer:YES];

    [g_offscreen_window setContentView:g_offscreen_view];
    if (gui_use_visible_iosurface_host()) {
        [g_offscreen_window orderFront:nil];
    } else {
        [g_offscreen_window orderBack:nil];
    }

    loader->open_editor((__bridge void *)g_offscreen_view);
    gui_refresh_capture_layout(true, true);
    const uint32_t surface_id = g_surface ? IOSurfaceGetID(g_surface) : 0;
    g_active_loader = loader;
    g_editor_open = true;
    g_iosurface_mode = true;
    gui_capture_iosurface_if_needed();
    gui_request_capture_burst();

    fprintf(stderr, "bridge: IOSurface editor opened (%dx%d) surfaceID=%u\n",
            width, height, surface_id);
    return surface_id;
}

static void capture_to_iosurface() {
    if (!g_surface || !g_offscreen_view) return;
    const uint64_t start_ns = gui_now_ns();
    ScopedEditorFrameLock editor_lock(g_capture_loader);

    size_t width = IOSurfaceGetWidth(g_surface);
    size_t height = IOSurfaceGetHeight(g_surface);
    size_t bytesPerRow = IOSurfaceGetBytesPerRow(g_surface);

    IOSurfaceLock(g_surface, 0, nullptr);
    void *base = IOSurfaceGetBaseAddress(g_surface);

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGBitmapInfo bitmap_info = static_cast<CGBitmapInfo>(
        static_cast<uint32_t>(kCGImageAlphaPremultipliedFirst) |
        static_cast<uint32_t>(kCGBitmapByteOrder32Little));
    CGContextRef ctx = CGBitmapContextCreate(
        base, width, height, 8, bytesPerRow, cs, bitmap_info);

    if (ctx) {
        [g_offscreen_view displayIfNeeded];
        if (g_offscreen_window) [g_offscreen_window displayIfNeeded];
        [g_offscreen_view layoutSubtreeIfNeeded];
        gui_prepare_layer_tree([g_offscreen_view layer]);
        [CATransaction flush];
        CALayer *layer = gui_capture_root_layer();
        if (layer) {
            CGContextSaveGState(ctx);
            CGContextTranslateCTM(ctx, -NSMinX(g_capture_rect), -NSMinY(g_capture_rect));
            [layer renderInContext:ctx];
            CGContextRestoreGState(ctx);
        }
        gui_composite_auxiliary_windows(ctx);

        CGContextRelease(ctx);
    }

    CGColorSpaceRelease(cs);

    IOSurfaceUnlock(g_surface, 0, nullptr);
    g_capture_count += 1;
    g_capture_time_ns += (gui_now_ns() - start_ns);
    gui_log_iosurface_telemetry_if_needed();
}

void gui_capture_iosurface_if_needed() {
    if (!g_iosurface_mode) return;
    gui_log_window_snapshot_if_changed();
    gui_dump_editor_tree_once();
    capture_to_iosurface();
}

void gui_forward_mouse(const IpcMouseEvent &ev) {
    if (!g_offscreen_view || !g_offscreen_window) return;
    g_input_event_count += 1;
    ScopedEditorFrameLock editor_lock(g_capture_loader);

    NSPoint point = NSMakePoint(ev.x + NSMinX(g_capture_rect),
                                ev.y + NSMinY(g_capture_rect));
    NSEventType type;
    switch (ev.type) {
    case 0: type = NSEventTypeMouseMoved; break;
    case 1: type = NSEventTypeLeftMouseDown; break;
    case 2: type = NSEventTypeLeftMouseUp; break;
    case 3: type = NSEventTypeLeftMouseDragged; break;
    case 4: type = NSEventTypeScrollWheel; break;
    default: return;
    }

    if (ev.button == 1) {
        if (ev.type == 1) type = NSEventTypeRightMouseDown;
        else if (ev.type == 2) type = NSEventTypeRightMouseUp;
        else if (ev.type == 3) type = NSEventTypeRightMouseDragged;
    }

    if (type == NSEventTypeScrollWheel) {
        CGEventRef cg_event = CGEventCreateScrollWheelEvent(
            nullptr, kCGScrollEventUnitPixel, 2,
            (int32_t)ev.scroll_dy, (int32_t)ev.scroll_dx);
        NSEvent *event = [NSEvent eventWithCGEvent:cg_event];
        CFRelease(cg_event);
        NSView *target_view = gui_pick_mouse_target(point);
        if (target_view && g_offscreen_window) {
            [g_offscreen_window makeFirstResponder:target_view];
            g_key_target = target_view;
        }
        gui_dispatch_mouse_event(target_view, event);
        [CATransaction flush];
        gui_capture_iosurface_if_needed();
        gui_request_capture_burst();
    } else {
        NSView *target_view = gui_pick_mouse_target(point);
        if (type == NSEventTypeLeftMouseDown || type == NSEventTypeRightMouseDown ||
            type == NSEventTypeOtherMouseDown) {
            g_mouse_target_view = target_view;
            g_key_target = target_view;
            g_mouse_click_count = 1;
        } else if (type == NSEventTypeLeftMouseUp || type == NSEventTypeRightMouseUp ||
                   type == NSEventTypeOtherMouseUp) {
            g_mouse_target_view = nil;
        }
        const NSPoint window_point = target_view
            ? [target_view convertPoint:point toView:nil]
            : point;
        const NSPoint screen_point = [g_offscreen_window convertPointToScreen:window_point];
        NSInteger click_count = 0;
        CGFloat pressure = 0.0f;
        switch (type) {
        case NSEventTypeLeftMouseDown:
        case NSEventTypeRightMouseDown:
        case NSEventTypeOtherMouseDown:
            click_count = std::max<NSInteger>(1, g_mouse_click_count);
            pressure = 1.0f;
            break;
        case NSEventTypeLeftMouseDragged:
        case NSEventTypeRightMouseDragged:
        case NSEventTypeOtherMouseDragged:
            click_count = std::max<NSInteger>(1, g_mouse_click_count);
            pressure = 1.0f;
            break;
        case NSEventTypeLeftMouseUp:
        case NSEventTypeRightMouseUp:
        case NSEventTypeOtherMouseUp:
            click_count = std::max<NSInteger>(1, g_mouse_click_count);
            pressure = 0.0f;
            g_mouse_click_count = 0;
            break;
        default:
            click_count = 0;
            pressure = 0.0f;
            break;
        }
        CGEventType cg_type = gui_cg_mouse_event_type(ev);
        CGEventRef cg_event = CGEventCreateMouseEvent(
            nullptr,
            cg_type,
            CGPointMake(screen_point.x, screen_point.y),
            gui_cg_mouse_button(ev.button));
        if (!cg_event) return;
        CGEventSetIntegerValueField(cg_event, kCGMouseEventClickState, click_count);
        CGEventSetIntegerValueField(cg_event, kCGMouseEventNumber, g_mouse_event_number++);
        CGEventSetDoubleValueField(cg_event, kCGMouseEventPressure, pressure);
        if (ev.button == 2) {
            CGEventSetIntegerValueField(cg_event, kCGMouseEventButtonNumber, 2);
        }
        NSEvent *event = [NSEvent eventWithCGEvent:cg_event];
        CFRelease(cg_event);
        if (target_view && g_offscreen_window) {
            [g_offscreen_window makeFirstResponder:target_view];
        }
        gui_dispatch_mouse_event(target_view, event);
        [CATransaction flush];
        gui_capture_iosurface_if_needed();
        gui_request_capture_burst();
    }

}

void gui_forward_key(const IpcKeyEvent &ev) {
    if (!g_offscreen_window) return;
    g_input_event_count += 1;
    ScopedEditorFrameLock editor_lock(g_capture_loader);

    NSEventType type = (ev.type == 0) ? NSEventTypeKeyDown : NSEventTypeKeyUp;
    NSString *chars = [NSString stringWithFormat:@"%c", ev.character];

    NSEvent *event = [NSEvent keyEventWithType:type
                                      location:NSZeroPoint
                                 modifierFlags:ev.modifiers
                                     timestamp:CACurrentMediaTime()
                                  windowNumber:[g_offscreen_window windowNumber]
                                       context:nil
                                    characters:chars
                   charactersIgnoringModifiers:chars
                                    isARepeat:NO
                                      keyCode:ev.keycode];
    gui_dispatch_key_event(event);
    [CATransaction flush];
    gui_capture_iosurface_if_needed();
    gui_request_capture_burst();
}

bool gui_is_iosurface_mode() {
    return g_iosurface_mode;
}

void gui_close_iosurface_state() {
    if (g_offscreen_window) {
        [g_offscreen_window orderOut:nil];
        g_offscreen_window = nil;
    }
    g_mouse_target_view = nil;
    g_key_target = nil;
    g_capture_loader = nullptr;
    g_offscreen_view = nil;
    g_capture_rect = NSZeroRect;
    if (g_surface) {
        CFRelease(g_surface);
        g_surface = nullptr;
    }
    g_iosurface_mode = false;
}
