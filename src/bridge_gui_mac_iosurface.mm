//
// Bridge GUI — macOS IOSurface editor hosting.
//

#import "bridge_gui_mac_internal.h"

#import <IOSurface/IOSurface.h>
#import <QuartzCore/QuartzCore.h>

#include "ipc.h"

static IOSurfaceRef g_surface = nullptr;
static NSView *g_offscreen_view = nil;
static NSWindow *g_offscreen_window = nil;

uint32_t gui_open_editor_iosurface(BridgeLoader *loader, int width, int height) {
    if (!loader || !loader->has_editor()) return 0;

    NSDictionary *props = @{
        (id)kIOSurfaceWidth: @(width),
        (id)kIOSurfaceHeight: @(height),
        (id)kIOSurfaceBytesPerElement: @4,
        (id)kIOSurfaceBytesPerRow: @(width * 4),
        (id)kIOSurfacePixelFormat: @((uint32_t)'BGRA'),
    };

    g_surface = IOSurfaceCreate((__bridge CFDictionaryRef)props);
    if (!g_surface) {
        fprintf(stderr, "bridge: failed to create IOSurface\n");
        return 0;
    }

    uint32_t surface_id = IOSurfaceGetID(g_surface);

    NSRect frame = NSMakeRect(0, -10000, width, height);
    g_offscreen_window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskBorderless
                    backing:NSBackingStoreBuffered
                      defer:NO];
    [g_offscreen_window setReleasedWhenClosed:NO];

    g_offscreen_view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
    [g_offscreen_view setWantsLayer:YES];

    [g_offscreen_window setContentView:g_offscreen_view];
    [g_offscreen_window orderBack:nil];

    loader->open_editor((__bridge void *)g_offscreen_view);
    g_active_loader = loader;
    g_editor_open = true;
    g_iosurface_mode = true;

    fprintf(stderr, "bridge: IOSurface editor opened (%dx%d) surfaceID=%u\n",
            width, height, surface_id);
    return surface_id;
}

static void capture_to_iosurface() {
    if (!g_surface || !g_offscreen_view) return;

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
        CGContextTranslateCTM(ctx, 0, height);
        CGContextScaleCTM(ctx, 1.0, -1.0);

        CALayer *layer = [g_offscreen_view layer];
        if (layer) [layer renderInContext:ctx];

        CGContextRelease(ctx);
    }

    CGColorSpaceRelease(cs);
    IOSurfaceUnlock(g_surface, 0, nullptr);
}

void gui_capture_iosurface_if_needed() {
    if (g_iosurface_mode) capture_to_iosurface();
}

void gui_forward_mouse(const IpcMouseEvent &ev) {
    if (!g_offscreen_view || !g_offscreen_window) return;

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
        NSEvent *event = [NSEvent eventWithCGEvent:CGEventCreateScrollWheelEvent(
            nullptr, kCGScrollEventUnitPixel, 2,
            (int32_t)ev.scroll_dy, (int32_t)ev.scroll_dx)];
        [g_offscreen_view scrollWheel:event];
    } else {
        NSEvent *event = [NSEvent mouseEventWithType:type
                                            location:point
                                       modifierFlags:0
                                           timestamp:CACurrentMediaTime()
                                        windowNumber:[g_offscreen_window windowNumber]
                                             context:nil
                                         eventNumber:0
                                          clickCount:(ev.type == 1) ? 1 : 0
                                            pressure:(ev.type == 1 || ev.type == 3) ? 1.0f : 0.0f];
        [g_offscreen_view.subviews.firstObject ?: g_offscreen_view mouseDown:event];
        [NSApp sendEvent:event];
    }
}

void gui_forward_key(const IpcKeyEvent &ev) {
    if (!g_offscreen_window) return;

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
    [NSApp sendEvent:event];
}

bool gui_is_iosurface_mode() {
    return g_iosurface_mode;
}

void gui_close_iosurface_state() {
    if (g_offscreen_window) {
        [g_offscreen_window orderOut:nil];
        g_offscreen_window = nil;
    }
    g_offscreen_view = nil;
    if (g_surface) {
        CFRelease(g_surface);
        g_surface = nullptr;
    }
    g_iosurface_mode = false;
}
