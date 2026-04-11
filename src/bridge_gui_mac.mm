//
// Bridge GUI — macOS (Cocoa) implementation with header bar.
// Opens the plugin editor in a floating NSWindow with a Keepsake
// toolbar showing plugin name, format, architecture, and isolation.
//

#import <Cocoa/Cocoa.h>
#include "bridge_gui.h"
#include <cstdio>

// VST2 ERect (not in VeSTige header)
struct ERect { int16_t top, left, bottom, right; };

static const CGFloat HEADER_HEIGHT = 28.0;

// --- Flipped container view (0,0 at top-left) ---

@interface KeepsakeFlippedView : NSView
@end

@implementation KeepsakeFlippedView
- (BOOL)isFlipped { return YES; }
@end

// --- Header bar view ---

@interface KeepsakeHeaderView : KeepsakeFlippedView
@property (nonatomic, strong) NSString *pluginName;
@property (nonatomic, strong) NSString *formatBadge;
@property (nonatomic, strong) NSString *archBadge;
@property (nonatomic, strong) NSString *isolationBadge;
@end

@implementation KeepsakeHeaderView

- (void)drawRect:(NSRect)dirtyRect {
    // Background
    [[NSColor colorWithWhite:0.15 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    // Separator line at bottom
    [[NSColor colorWithWhite:0.3 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(0, 0, self.bounds.size.width, 1));

    NSDictionary *nameAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: [NSColor colorWithWhite:0.9 alpha:1.0]
    };
    NSDictionary *badgeAttrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:9 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: [NSColor colorWithWhite:0.7 alpha:1.0]
    };

    // Plugin name (left aligned) — flipped coords, y grows downward
    CGFloat x = 10;
    CGFloat textY = 6;
    if (self.pluginName) {
        [self.pluginName drawAtPoint:NSMakePoint(x, textY)
                      withAttributes:nameAttrs];
        x += [self.pluginName sizeWithAttributes:nameAttrs].width + 12;
    }

    // Badges (right-aligned)
    __block CGFloat rx = self.bounds.size.width - 10;

    auto drawBadge = ^(NSString *text, NSColor *bg) {
        if (!text || text.length == 0) return;
        NSSize sz = [text sizeWithAttributes:badgeAttrs];
        CGFloat bw = sz.width + 8;
        CGFloat bh = 16;
        CGFloat bx = rx - bw;
        CGFloat by = (HEADER_HEIGHT - bh) / 2; // centered vertically

        NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:
            NSMakeRect(bx, by, bw, bh) xRadius:3 yRadius:3];
        [bg setFill];
        [path fill];
        [text drawAtPoint:NSMakePoint(bx + 4, by + 2)
           withAttributes:badgeAttrs];
        rx = bx - 6;
    };

    drawBadge(self.isolationBadge,
              [NSColor colorWithRed:0.3 green:0.3 blue:0.5 alpha:1.0]);
    drawBadge(self.archBadge,
              [NSColor colorWithRed:0.3 green:0.45 blue:0.3 alpha:1.0]);
    drawBadge(self.formatBadge,
              [NSColor colorWithRed:0.45 green:0.3 blue:0.2 alpha:1.0]);
}

@end

// --- State ---

static NSWindow *g_window = nil;
static KeepsakeHeaderView *g_header = nil;
static NSView *g_editor_container = nil;
static BridgeLoader *g_active_loader = nil;
static bool g_editor_open = false;
static bool g_iosurface_mode = false;
static int g_current_width = 0;
static int g_current_height = 0;

// --- Public API ---

void gui_init() {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp finishLaunching];
}

bool gui_open_editor(BridgeLoader *loader, const EditorHeaderInfo &header) {
    if (!loader || !loader->has_editor()) return false;
    if (g_editor_open) return true;

    int w = 640, h = 480;
    loader->get_editor_rect(w, h);

    // Create window (editor height + header height)
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

    // Use a flipped content view so layout is top-down
    KeepsakeFlippedView *content = [[KeepsakeFlippedView alloc]
        initWithFrame:NSMakeRect(0, 0, w, h + HEADER_HEIGHT)];
    [g_window setContentView:content];

    // Editor container below the header — added FIRST so header draws on top
    g_editor_container = [[KeepsakeFlippedView alloc]
        initWithFrame:NSMakeRect(0, HEADER_HEIGHT, w, h)];
    [g_editor_container setWantsLayer:YES];
    g_editor_container.layer.masksToBounds = YES; // clip plugin content
    [content addSubview:g_editor_container];

    // Header bar at the top — added LAST so it draws above the editor
    g_header = [[KeepsakeHeaderView alloc]
        initWithFrame:NSMakeRect(0, 0, w, HEADER_HEIGHT)];
    g_header.pluginName = [NSString stringWithUTF8String:
                           header.plugin_name.c_str()];
    g_header.formatBadge = [NSString stringWithUTF8String:
                            header.format.c_str()];
    g_header.archBadge = [NSString stringWithUTF8String:
                          header.architecture.c_str()];
    g_header.isolationBadge = [NSString stringWithUTF8String:
                               header.isolation.c_str()];
    [g_header setWantsLayer:YES]; // ensure it composites above editor
    [content addSubview:g_header];

    // Open the plugin editor into the container
    loader->open_editor((__bridge void *)g_editor_container);
    g_active_loader = loader;

    // Watch for subview frame changes (instant resize tracking)
    [g_editor_container setPostsFrameChangedNotifications:YES];
    for (NSView *subview in [g_editor_container subviews]) {
        [subview setPostsFrameChangedNotifications:YES];
    }
    [[NSNotificationCenter defaultCenter]
        addObserverForName:NSViewFrameDidChangeNotification
        object:nil
        queue:nil
        usingBlock:^(NSNotification *note) {
            NSView *v = [note object];
            // Only react to subviews of our editor container
            if (!g_editor_open || !g_editor_container) return;
            if ([v superview] != g_editor_container && v != g_editor_container) return;

            NSSize sz = [v frame].size;
            int nw = static_cast<int>(sz.width);
            int nh = static_cast<int>(sz.height);
            if (nw > 0 && nh > 0 && (nw != g_current_width || nh != g_current_height)) {
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
            }
        }];

    // Re-check size after open (some plugins report correct size only after effEditOpen)
    int newW = w, newH = h;
    loader->get_editor_rect(newW, newH);
    if (newW != w || newH != h) {
        [g_window setContentSize:NSMakeSize(newW, newH + HEADER_HEIGHT)];
        [g_header setFrame:NSMakeRect(0, 0, newW, HEADER_HEIGHT)];
        [g_editor_container setFrame:NSMakeRect(0, HEADER_HEIGHT, newW, newH)];
        w = newW; h = newH;
    }

    // Track current size for resize detection
    if (newW != w || newH != h) { w = newW; h = newH; }
    g_current_width = w;
    g_current_height = h;

    [g_window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    g_editor_open = true;
    fprintf(stderr, "bridge: editor opened (%dx%d) with header bar\n", w, h);
    return true;
}

bool gui_open_editor_embedded(BridgeLoader * /*loader*/, uint64_t /*native_handle*/) {
    // macOS does not support cross-process NSView embedding.
    // Floating windows are the permanent approach on macOS.
    fprintf(stderr, "bridge: embedded editor not supported on macOS (use floating)\n");
    return false;
}

void gui_close_editor(BridgeLoader *loader) {
    if (!g_editor_open) return;

    if (loader) loader->close_editor();
    else if (g_active_loader) g_active_loader->close_editor();

    [[NSNotificationCenter defaultCenter] removeObserver:nil
        name:NSViewFrameDidChangeNotification object:nil];
    if (g_window) {
        [g_window orderOut:nil];
        g_window = nil;
    }
    g_header = nil;
    g_editor_container = nil;
    g_active_loader = nil;
    g_editor_open = false;
    fprintf(stderr, "bridge: editor closed\n");
}

bool gui_get_editor_rect(BridgeLoader *loader, int &width, int &height) {
    if (!loader) return false;
    return loader->get_editor_rect(width, height);
}

// Forward declarations (IOSurface mode, defined below)
static void capture_to_iosurface();

void gui_idle(BridgeLoader *loader) {
    if (!g_editor_open) return;

    @autoreleasepool {
        NSEvent *event;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:nil
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES])) {
            [NSApp sendEvent:event];
        }
    }

    // Editor idle tick — the plugin may need periodic updates
    BridgeLoader *active = loader ? loader : g_active_loader;
    if (active) {
        active->editor_idle();

        // Capture view content into IOSurface for cross-process display
        if (gui_is_iosurface_mode()) {
            capture_to_iosurface();
        }
    }

    if (g_window && ![g_window isVisible]) {
        gui_close_editor(loader);
    }
}

bool gui_is_open() {
    return g_editor_open;
}

// --- IOSurface embedded mode ---

#import <IOSurface/IOSurface.h>
#import <QuartzCore/QuartzCore.h>
#include "ipc.h"

static IOSurfaceRef g_surface = nullptr;
static NSView *g_offscreen_view = nil;
static NSWindow *g_offscreen_window = nil;

uint32_t gui_open_editor_iosurface(BridgeLoader *loader, int width, int height) {
    if (!loader || !loader->has_editor()) return 0;

    // Create IOSurface
    NSDictionary *props = @{
        (id)kIOSurfaceWidth: @(width),
        (id)kIOSurfaceHeight: @(height),
        (id)kIOSurfaceBytesPerElement: @4,
        (id)kIOSurfaceBytesPerRow: @(width * 4),
        (id)kIOSurfacePixelFormat: @((uint32_t)'BGRA'),
        (id)kIOSurfaceIsGlobal: @YES,  // accessible from other processes
    };

    g_surface = IOSurfaceCreate((__bridge CFDictionaryRef)props);
    if (!g_surface) {
        fprintf(stderr, "bridge: failed to create IOSurface\n");
        return 0;
    }

    uint32_t surface_id = IOSurfaceGetID(g_surface);

    // Create an offscreen window with a layer-backed view
    // The plugin will draw into this view, which renders to the IOSurface
    NSRect frame = NSMakeRect(0, -10000, width, height); // offscreen position
    g_offscreen_window = [[NSWindow alloc]
        initWithContentRect:frame
        styleMask:NSWindowStyleMaskBorderless
        backing:NSBackingStoreBuffered
        defer:NO];
    [g_offscreen_window setReleasedWhenClosed:NO];

    g_offscreen_view = [[KeepsakeFlippedView alloc]
        initWithFrame:NSMakeRect(0, 0, width, height)];
    [g_offscreen_view setWantsLayer:YES];

    [g_offscreen_window setContentView:g_offscreen_view];
    [g_offscreen_window orderBack:nil]; // show but behind everything

    // Open the plugin editor into the offscreen view
    loader->open_editor((__bridge void *)g_offscreen_view);
    g_active_loader = loader;
    g_editor_open = true;
    g_iosurface_mode = true;

    fprintf(stderr, "bridge: IOSurface editor opened (%dx%d) surfaceID=%u\n",
            width, height, surface_id);
    return surface_id;
}

// Capture the offscreen view's content into the IOSurface.
// Called from gui_idle at ~60fps.
static void capture_to_iosurface() {
    if (!g_surface || !g_offscreen_view) return;

    size_t width = IOSurfaceGetWidth(g_surface);
    size_t height = IOSurfaceGetHeight(g_surface);
    size_t bytesPerRow = IOSurfaceGetBytesPerRow(g_surface);

    IOSurfaceLock(g_surface, 0, nullptr);
    void *base = IOSurfaceGetBaseAddress(g_surface);

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        base, width, height, 8, bytesPerRow, cs,
        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);

    if (ctx) {
        // Flip the context (CoreGraphics is bottom-up, views are top-down)
        CGContextTranslateCTM(ctx, 0, height);
        CGContextScaleCTM(ctx, 1.0, -1.0);

        // Render the view hierarchy into the IOSurface
        NSGraphicsContext *nsCtx = [NSGraphicsContext
            graphicsContextWithCGContext:ctx flipped:YES];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:nsCtx];

        // Render the entire view tree
        [g_offscreen_view displayRectIgnoringOpacity:[g_offscreen_view bounds]
                                           inContext:nsCtx];

        [NSGraphicsContext restoreGraphicsState];
        CGContextRelease(ctx);
    }

    CGColorSpaceRelease(cs);
    IOSurfaceUnlock(g_surface, 0, nullptr);
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
        switch (type) {
        case NSEventTypeLeftMouseDown:
        case NSEventTypeRightMouseDown:
            [NSApp sendEvent:event]; break;
        case NSEventTypeLeftMouseUp:
        case NSEventTypeRightMouseUp:
            [NSApp sendEvent:event]; break;
        case NSEventTypeLeftMouseDragged:
        case NSEventTypeRightMouseDragged:
            [NSApp sendEvent:event]; break;
        default:
            [NSApp sendEvent:event]; break;
        }
    }
}

void gui_forward_key(const IpcKeyEvent &ev) {
    // Key forwarding — basic implementation
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
