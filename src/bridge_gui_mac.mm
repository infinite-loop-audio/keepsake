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

// --- Header bar view ---

@interface KeepsakeHeaderView : NSView
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

    // Plugin name (left aligned)
    CGFloat x = 10;
    CGFloat textY = 7;
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
        CGFloat by = (HEADER_HEIGHT - bh) / 2;

        NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:
            NSMakeRect(bx, by, bw, bh) xRadius:3 yRadius:3];
        [bg setFill];
        [path fill];
        [text drawAtPoint:NSMakePoint(bx + 4, by + 1.5)
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

    NSView *content = [g_window contentView];

    // Header bar at the top
    g_header = [[KeepsakeHeaderView alloc]
        initWithFrame:NSMakeRect(0, h, w, HEADER_HEIGHT)];
    g_header.pluginName = [NSString stringWithUTF8String:
                           header.plugin_name.c_str()];
    g_header.formatBadge = [NSString stringWithUTF8String:
                            header.format.c_str()];
    g_header.archBadge = [NSString stringWithUTF8String:
                          header.architecture.c_str()];
    g_header.isolationBadge = [NSString stringWithUTF8String:
                               header.isolation.c_str()];
    [g_header setAutoresizingMask:NSViewWidthSizable | NSViewMinYMargin];
    [content addSubview:g_header];

    // Editor container below the header
    g_editor_container = [[NSView alloc]
        initWithFrame:NSMakeRect(0, 0, w, h)];
    [g_editor_container setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [content addSubview:g_editor_container];

    // Open the plugin editor into the container
    loader->open_editor((__bridge void *)g_editor_container);
    g_active_loader = loader;

    // Re-check size after open
    int newW = w, newH = h;
    loader->get_editor_rect(newW, newH);
    if (newW != w || newH != h) {
        [g_window setContentSize:NSMakeSize(newW, newH + HEADER_HEIGHT)];
        [g_editor_container setFrameSize:NSMakeSize(newW, newH)];
        [g_header setFrame:NSMakeRect(0, newH, newW, HEADER_HEIGHT)];
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

    BridgeLoader *active = loader ? loader : g_active_loader;
    if (active) {
        active->editor_idle();

        // Check if the plugin's editor has resized
        int newW = 0, newH = 0;
        if (active->get_editor_rect(newW, newH) &&
            (newW != g_current_width || newH != g_current_height) &&
            newW > 0 && newH > 0) {
            g_current_width = newW;
            g_current_height = newH;

            NSRect frame = [g_window frame];
            NSRect content = [g_window contentRectForFrameRect:frame];
            CGFloat dh = (newH + HEADER_HEIGHT) - content.size.height;
            CGFloat dw = newW - content.size.width;

            frame.size.width += dw;
            frame.size.height += dh;
            frame.origin.y -= dh; // grow downward

            [g_window setFrame:frame display:YES animate:NO];
            [g_editor_container setFrameSize:NSMakeSize(newW, newH)];
            [g_header setFrame:NSMakeRect(0, newH, newW, HEADER_HEIGHT)];

            fprintf(stderr, "bridge: editor resized to %dx%d\n", newW, newH);
        }
    }

    if (g_window && ![g_window isVisible]) {
        gui_close_editor(loader);
    }
}

bool gui_is_open() {
    return g_editor_open;
}
