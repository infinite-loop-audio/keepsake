//
// Bridge GUI — macOS IOSurface editor hosting.
//

#import "bridge_gui_mac_internal.h"

#import <IOSurface/IOSurface.h>
#import <QuartzCore/QuartzCore.h>

#include "debug_log.h"
#include "ipc.h"
#include <mach/mach_time.h>

static IOSurfaceRef g_surface = nullptr;
static NSView *g_offscreen_view = nil;
static NSWindow *g_offscreen_window = nil;
static NSView *g_mouse_target_view = nil;
static NSResponder *g_key_target = nil;
static bool g_iosurface_telemetry_enabled = false;
static uint64_t g_capture_count = 0;
static uint64_t g_input_event_count = 0;
static uint64_t g_capture_time_ns = 0;
static uint64_t g_last_telemetry_log_ns = 0;

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

static bool gui_resize_iosurface_host(int width, int height) {
    if (!g_offscreen_window || !g_offscreen_view || width <= 0 || height <= 0) {
        return false;
    }
    IOSurfaceRef surface = gui_create_iosurface(width, height);
    if (!surface) {
        keepsake_debug_log("bridge/mac: failed to resize IOSurface host to %dx%d\n", width, height);
        return false;
    }
    if (g_surface) {
        CFRelease(g_surface);
    }
    g_surface = surface;
    g_current_width = width;
    g_current_height = height;
    [g_offscreen_view setFrame:NSMakeRect(0, 0, width, height)];
    [g_offscreen_window setContentSize:NSMakeSize(width, height)];
    [g_offscreen_window setFrame:NSMakeRect(0, -10000, width, height) display:NO];
    keepsake_debug_log("bridge/mac: resized iosurface host to %dx%d surfaceID=%u\n",
                       width, height, IOSurfaceGetID(g_surface));
    return true;
}

bool gui_resize_editor_iosurface(int width, int height, uint32_t &surface_id) {
    if (!g_iosurface_mode) return false;
    if (!gui_resize_iosurface_host(width, height)) return false;
    surface_id = g_surface ? IOSurfaceGetID(g_surface) : 0;
    gui_capture_iosurface_if_needed();
    return surface_id != 0;
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

static NSView *gui_pick_mouse_target(NSPoint point) {
    NSView *target = nil;
    if (g_mouse_target_view) {
        target = g_mouse_target_view;
    } else if (g_offscreen_view) {
        target = [g_offscreen_view hitTest:point];
    }
    if (!target) {
        target = gui_mouse_fallback_target();
    }
    return target;
}

static void gui_dispatch_mouse_event(NSView *target_view, NSEvent *event) {
    if (!target_view || !event) return;
    switch (event.type) {
    case NSEventTypeMouseMoved:
        if ([target_view respondsToSelector:@selector(mouseMoved:)]) {
            [target_view mouseMoved:event];
        }
        break;
    case NSEventTypeLeftMouseDown:
        if ([target_view respondsToSelector:@selector(mouseDown:)]) {
            [target_view mouseDown:event];
        }
        break;
    case NSEventTypeLeftMouseUp:
        if ([target_view respondsToSelector:@selector(mouseUp:)]) {
            [target_view mouseUp:event];
        }
        break;
    case NSEventTypeLeftMouseDragged:
        if ([target_view respondsToSelector:@selector(mouseDragged:)]) {
            [target_view mouseDragged:event];
        }
        break;
    case NSEventTypeRightMouseDown:
        if ([target_view respondsToSelector:@selector(rightMouseDown:)]) {
            [target_view rightMouseDown:event];
        }
        break;
    case NSEventTypeRightMouseUp:
        if ([target_view respondsToSelector:@selector(rightMouseUp:)]) {
            [target_view rightMouseUp:event];
        }
        break;
    case NSEventTypeRightMouseDragged:
        if ([target_view respondsToSelector:@selector(rightMouseDragged:)]) {
            [target_view rightMouseDragged:event];
        }
        break;
    case NSEventTypeOtherMouseDown:
        if ([target_view respondsToSelector:@selector(otherMouseDown:)]) {
            [target_view otherMouseDown:event];
        }
        break;
    case NSEventTypeOtherMouseUp:
        if ([target_view respondsToSelector:@selector(otherMouseUp:)]) {
            [target_view otherMouseUp:event];
        }
        break;
    case NSEventTypeOtherMouseDragged:
        if ([target_view respondsToSelector:@selector(otherMouseDragged:)]) {
            [target_view otherMouseDragged:event];
        }
        break;
    case NSEventTypeScrollWheel:
        if ([target_view respondsToSelector:@selector(scrollWheel:)]) {
            [target_view scrollWheel:event];
        }
        break;
    default:
        break;
    }
}

static void gui_dispatch_key_event(NSEvent *event) {
    if (!event) return;
    NSResponder *target = g_key_target;
    if (!target && g_offscreen_window) {
        target = [g_offscreen_window firstResponder];
    }
    if (!target) {
        target = gui_mouse_fallback_target();
    }
    if (!target) return;
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
    g_mouse_target_view = nil;
    g_key_target = nil;

    g_surface = gui_create_iosurface(width, height);
    if (!g_surface) {
        fprintf(stderr, "bridge: failed to create IOSurface\n");
        return 0;
    }
    g_current_width = width;
    g_current_height = height;

    NSRect frame = NSMakeRect(0, -10000, width, height);
    g_offscreen_window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskBorderless
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [g_offscreen_window setReleasedWhenClosed:NO];

    g_offscreen_view = [[KeepsakeIOSurfaceHostView alloc]
        initWithFrame:NSMakeRect(0, 0, width, height)];
    [g_offscreen_view setWantsLayer:YES];

    [g_offscreen_window setContentView:g_offscreen_view];
    [g_offscreen_window orderBack:nil];

    loader->open_editor((__bridge void *)g_offscreen_view);
    const uint32_t surface_id = g_surface ? IOSurfaceGetID(g_surface) : 0;
    g_active_loader = loader;
    g_editor_open = true;
    g_iosurface_mode = true;
    gui_capture_iosurface_if_needed();

    fprintf(stderr, "bridge: IOSurface editor opened (%dx%d) surfaceID=%u\n",
            width, height, surface_id);
    return surface_id;
}

static void capture_to_iosurface() {
    if (!g_surface || !g_offscreen_view) return;
    const uint64_t start_ns = gui_now_ns();

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
        [CATransaction flush];
        CALayer *layer = [g_offscreen_view layer];
        if (layer) [layer renderInContext:ctx];

        CGContextRelease(ctx);
    }

    CGColorSpaceRelease(cs);
    IOSurfaceUnlock(g_surface, 0, nullptr);
    g_capture_count += 1;
    g_capture_time_ns += (gui_now_ns() - start_ns);
    gui_log_iosurface_telemetry_if_needed();
}

void gui_capture_iosurface_if_needed() {
    if (g_iosurface_mode) capture_to_iosurface();
}

void gui_forward_mouse(const IpcMouseEvent &ev) {
    if (!g_offscreen_view || !g_offscreen_window) return;
    g_input_event_count += 1;

    NSPoint point = NSMakePoint(ev.x, ev.y);
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
    } else {
        NSView *target_view = gui_pick_mouse_target(point);
        if (type == NSEventTypeLeftMouseDown || type == NSEventTypeRightMouseDown) {
            g_mouse_target_view = target_view;
            g_key_target = target_view;
        } else if (type == NSEventTypeLeftMouseUp || type == NSEventTypeRightMouseUp) {
            g_mouse_target_view = nil;
        }
        const NSPoint window_point = target_view
            ? [target_view convertPoint:point toView:nil]
            : point;
        NSEvent *event = [NSEvent mouseEventWithType:type
                                            location:window_point
                                       modifierFlags:0
                                           timestamp:CACurrentMediaTime()
                                        windowNumber:[g_offscreen_window windowNumber]
                                             context:nil
                                         eventNumber:0
                                          clickCount:(ev.type == 1) ? 1 : 0
                                            pressure:(ev.type == 1 || ev.type == 3) ? 1.0f : 0.0f];
        if (target_view && g_offscreen_window) {
            [g_offscreen_window makeFirstResponder:target_view];
        }
        gui_dispatch_mouse_event(target_view, event);
        [CATransaction flush];
        gui_capture_iosurface_if_needed();
    }
}

void gui_forward_key(const IpcKeyEvent &ev) {
    if (!g_offscreen_window) return;
    g_input_event_count += 1;

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
    g_offscreen_view = nil;
    if (g_surface) {
        CFRelease(g_surface);
        g_surface = nullptr;
    }
    g_iosurface_mode = false;
}
