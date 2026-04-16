#import "plugin_gui_mac_embed.h"

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <CoreImage/CoreImage.h>
#import <IOSurface/IOSurface.h>

#include "config.h"
#include "debug_log.h"
#include "plugin_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mach/mach_time.h>

static constexpr int kEditorEventTimeoutMs = 250;
static bool g_embed_telemetry_enabled = false;

enum class EmbedDrawBackend {
    CoreImage,
    CoreGraphics,
};

static uint64_t gui_now_ns() {
    static mach_timebase_info_data_t timebase = {0, 0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    const uint64_t t = mach_absolute_time();
    return (t * timebase.numer) / timebase.denom;
}

static bool send_mouse_event(KeepsakePlugin *kp, const IpcMouseEvent &event) {
    return kp && send_and_wait(kp, IPC_OP_EDITOR_MOUSE, &event, sizeof(event),
                               nullptr, kEditorEventTimeoutMs);
}

static bool send_key_event(KeepsakePlugin *kp, const IpcKeyEvent &event) {
    return kp && send_and_wait(kp, IPC_OP_EDITOR_KEY, &event, sizeof(event),
                               nullptr, kEditorEventTimeoutMs);
}

static uint32_t modifier_flags(NSEventModifierFlags flags) {
    return static_cast<uint32_t>(flags & NSEventModifierFlagDeviceIndependentFlagsMask);
}

static char event_character(NSEvent *event) {
    if (!event || event.characters.length == 0) return 0;
    return static_cast<char>([event.characters characterAtIndex:0] & 0x7f);
}

static uint64_t sample_surface_digest(IOSurfaceRef surface) {
    if (!surface) return 0;
    IOSurfaceLock(surface, kIOSurfaceLockReadOnly, nullptr);
    const size_t width = IOSurfaceGetWidth(surface);
    const size_t height = IOSurfaceGetHeight(surface);
    const size_t bytes_per_row = IOSurfaceGetBytesPerRow(surface);
    const uint8_t *base = static_cast<const uint8_t *>(IOSurfaceGetBaseAddress(surface));
    uint64_t digest = 1469598103934665603ull;
    if (base && width > 0 && height > 0 && bytes_per_row >= 4) {
        constexpr size_t sample_cols = 16;
        constexpr size_t sample_rows = 16;
        for (size_t row_idx = 0; row_idx < sample_rows; ++row_idx) {
            const size_t y = (row_idx * (height - 1)) / std::max<size_t>(1, sample_rows - 1);
            const uint8_t *row = base + (y * bytes_per_row);
            for (size_t col_idx = 0; col_idx < sample_cols; ++col_idx) {
                const size_t x = (col_idx * (width - 1)) / std::max<size_t>(1, sample_cols - 1);
                const uint32_t pixel = *(reinterpret_cast<const uint32_t *>(row + (x * 4)));
                digest ^= static_cast<uint64_t>(pixel);
                digest *= 1099511628211ull;
            }
        }
    }
    IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, nullptr);
    return digest;
}

static std::string describe_view_chain(NSView *view) {
    std::string out;
    NSView *cursor = view;
    bool first = true;
    while (cursor) {
        if (!first) out += " <- ";
        first = false;
        out += [[NSStringFromClass([cursor class]) stringByAppendingFormat:@"(%p)", cursor] UTF8String];
        cursor = cursor.superview;
    }
    return out;
}

static std::string normalize_attach_target(std::string value) {
    if (value.empty()) return "auto";
    if (value == "requested-parent" ||
        value == "content-view" ||
        value == "frame-superview" ||
        value == "auto") {
        return value;
    }
    return "auto";
}

static bool rect_reasonably_matches_surface(NSRect rect, int32_t surface_width, int32_t surface_height) {
    if (NSIsEmptyRect(rect)) return false;
    if (surface_width <= 0 || surface_height <= 0) return true;
    const double width = NSWidth(rect);
    const double height = NSHeight(rect);
    const double min_width = static_cast<double>(surface_width) * 0.75;
    const double min_height = static_cast<double>(surface_height) * 0.75;
    const double max_width = static_cast<double>(surface_width) * 1.5;
    const double max_height = static_cast<double>(surface_height) * 1.5;
    return width >= min_width && height >= min_height &&
           width <= max_width && height <= max_height;
}

static EmbedDrawBackend embed_draw_backend() {
    static bool loaded = false;
    static EmbedDrawBackend backend = EmbedDrawBackend::CoreGraphics;
    if (!loaded) {
        loaded = true;
        const char *mode = std::getenv("KEEPSAKE_MAC_EMBED_DRAW_BACKEND");
        if (mode && mode[0]) {
            if (std::strcmp(mode, "ci") == 0 || std::strcmp(mode, "coreimage") == 0) {
                backend = EmbedDrawBackend::CoreImage;
            } else if (std::strcmp(mode, "coregraphics") == 0 ||
                       std::strcmp(mode, "cg") == 0) {
                backend = EmbedDrawBackend::CoreGraphics;
            }
        }
    }
    return backend;
}

@interface KeepsakeIOSurfaceView : NSView {
@private
    KeepsakePlugin *_plugin;
    NSWindow *_hostWindow;
    NSView *_requestedParent;
    NSView *_hostParent;
    IOSurfaceRef _surface;
    CIContext *_ciContext;
    NSTrackingArea *_trackingArea;
    int32_t _surfaceWidth;
    int32_t _surfaceHeight;
    NSTimer *_refreshTimer;
    uint64_t _refreshCount;
    uint64_t _refreshTimeNs;
    uint64_t _lastTelemetryLogNs;
    uint64_t _surfaceDigest;
    uint64_t _surfaceDigestChanges;
    uint64_t _mouseEventCount;
    uint64_t _keyEventCount;
    uint64_t _drawCount;
    uint32_t _startupRepairFramesRemaining;
    NSRect _lastLoggedFrame;
    NSRect _lastLoggedBounds;
    NSRect _lastLoggedRequestedBounds;
    NSRect _lastLoggedHostBounds;
    bool _hasLoggedGeometry;
    bool _lockToRequestedParent;
}
- (instancetype)initWithPlugin:(KeepsakePlugin *)plugin
                     surfaceID:(uint32_t)surfaceID
                         width:(int32_t)width
                        height:(int32_t)height;
- (void)keepsakeScriptClickCenter;
- (void)keepsakeScriptTypeText:(NSString *)text;
- (void)attachToHostParent:(NSView *)parent;
- (NSView *)resolveAttachTarget;
- (NSRect)desiredFrameInHostParent;
- (NSRect)surfaceRectInView;
- (NSPoint)localSurfacePointForEvent:(NSEvent *)event;
- (void)enforceSurfaceGeometry;
- (void)refreshSurfaceFrame;
- (NSDictionary *)keepsakeDebugMetrics;
@end

@implementation KeepsakeIOSurfaceView

- (instancetype)initWithPlugin:(KeepsakePlugin *)plugin
                     surfaceID:(uint32_t)surfaceID
                         width:(int32_t)width
                        height:(int32_t)height {
    self = [super initWithFrame:NSMakeRect(0, 0, width, height)];
    if (!self) return nil;

    _plugin = plugin;
    g_embed_telemetry_enabled = getenv("KEEPSAKE_MAC_EMBED_TELEMETRY") != nullptr;
    _surface = IOSurfaceLookup(surfaceID);
    if (!_surface) return nil;
    _surfaceWidth = width;
    _surfaceHeight = height;
    _refreshCount = 0;
    _refreshTimeNs = 0;
    _lastTelemetryLogNs = 0;
    _surfaceDigest = 0;
    _surfaceDigestChanges = 0;
    _mouseEventCount = 0;
    _keyEventCount = 0;
    _drawCount = 0;
    _startupRepairFramesRemaining = 30;
    _lastLoggedFrame = NSZeroRect;
    _lastLoggedBounds = NSZeroRect;
    _lastLoggedRequestedBounds = NSZeroRect;
    _lastLoggedHostBounds = NSZeroRect;
    _hasLoggedGeometry = false;
    _lockToRequestedParent = false;

    self.wantsLayer = YES;
    self.wantsLayer = NO;
    _ciContext = [[CIContext contextWithOptions:nil] retain];
    _refreshTimer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 60.0)
                                                     target:self
                                                   selector:@selector(refreshSurfaceFrame)
                                                   userInfo:nil
                                                    repeats:YES];
    return self;
}

- (void)dealloc {
    if (_trackingArea) {
        [self removeTrackingArea:_trackingArea];
        _trackingArea = nil;
    }
    if (_refreshTimer) {
        [_refreshTimer invalidate];
        _refreshTimer = nil;
    }
    if (_ciContext) {
        [_ciContext release];
        _ciContext = nil;
    }
    if (_surface) {
        CFRelease(_surface);
        _surface = nullptr;
    }
    [_hostWindow release];
    _hostWindow = nil;
    [_requestedParent release];
    _requestedParent = nil;
    [_hostParent release];
    _hostParent = nil;
    [super dealloc];
}

- (BOOL)isFlipped { return YES; }

- (BOOL)acceptsFirstResponder { return YES; }

- (void)attachToHostParent:(NSView *)parent {
    if (parent) {
        if (_requestedParent != parent) {
            [_requestedParent release];
            _requestedParent = [parent retain];
        }
        _lockToRequestedParent = gui_mac_embed_attach_target() == "requested-parent";
        if (_lockToRequestedParent) {
            _startupRepairFramesRemaining = 0;
        }
        if (parent.window) {
            if (_hostWindow != parent.window) {
                [_hostWindow release];
                _hostWindow = [parent.window retain];
            }
        }
    }
    NSView *resolved_parent = [self resolveAttachTarget];
    if (_hostParent != resolved_parent) {
        [_hostParent release];
        _hostParent = [resolved_parent retain];
    }
    if (!_hostParent) {
        keepsake_debug_log("keepsake: embed attach unresolved requested-parent=%p\n",
                           _requestedParent);
        return;
    }
    if (!_hostParent.window || NSIsEmptyRect(_hostParent.bounds)) {
        keepsake_debug_log("keepsake: embed attach deferred parent=%p window=%p bounds=%.1fx%.1f\n",
                           _hostParent,
                           _hostParent.window,
                           NSWidth(_hostParent.bounds),
                           NSHeight(_hostParent.bounds));
        return;
    }
    if (self.superview != _hostParent) {
        self.frame = [self desiredFrameInHostParent];
        self.autoresizingMask = 0;
        [_hostParent addSubview:self positioned:NSWindowAbove relativeTo:nil];
        keepsake_debug_log("keepsake: embed attached parent=%p window=%p bounds=%.1fx%.1f\n",
                           _hostParent,
                           _hostParent.window,
                           NSWidth(_hostParent.bounds),
                           NSHeight(_hostParent.bounds));
        keepsake_debug_log("keepsake: embed attach chain requested=%s host=%s\n",
                           describe_view_chain(_requestedParent).c_str(),
                           describe_view_chain(_hostParent).c_str());
    }
}

- (NSView *)resolveAttachTarget {
    const std::string policy = gui_mac_embed_attach_target();
    if (_requestedParent && _requestedParent.window) {
        if (_hostWindow != _requestedParent.window) {
            [_hostWindow release];
            _hostWindow = [_requestedParent.window retain];
        }
    }
    if (_lockToRequestedParent && _requestedParent) {
        return _requestedParent;
    }
    if (policy == "requested-parent") {
        return _requestedParent;
    }
    if (_hostWindow && policy == "content-view") {
        return _hostWindow.contentView;
    }
    if (_hostWindow && policy == "frame-superview") {
        NSView *content = _hostWindow.contentView;
        return content && content.superview ? content.superview : content;
    }
    if (policy == "auto" && _hostWindow) {
        if (_requestedParent &&
            _requestedParent.window &&
            rect_reasonably_matches_surface(_requestedParent.bounds,
                                            _surfaceWidth,
                                            _surfaceHeight)) {
            return _requestedParent;
        }
        NSView *content = _hostWindow.contentView;
        if (content && !NSIsEmptyRect(content.bounds)) {
            return content;
        }
        if (content && content.superview && !NSIsEmptyRect(content.superview.bounds)) {
            return content.superview;
        }
    }

    NSView *candidate = _requestedParent ? _requestedParent : _hostParent;
    NSView *best = nil;
    while (candidate) {
        if (candidate.window && !NSIsEmptyRect(candidate.bounds)) {
            best = candidate;
        } else if (!best && candidate.window) {
            best = candidate;
        }
        candidate = candidate.superview;
    }
    if (!best && _requestedParent && _requestedParent.window) {
        _hostWindow = _requestedParent.window;
        best = _requestedParent.window.contentView;
    }
    if (!best && _hostWindow) {
        best = _hostWindow.contentView;
    }
    return best;
}

- (NSRect)desiredFrameInHostParent {
    if (!_hostParent) return self.frame;
    if (_requestedParent &&
        _requestedParent.window == _hostParent.window &&
        _requestedParent.superview) {
        NSRect rect = [_hostParent convertRect:_requestedParent.bounds fromView:_requestedParent];
        if (_surfaceWidth > 0) rect.size.width = _surfaceWidth;
        if (_surfaceHeight > 0) rect.size.height = _surfaceHeight;
        return rect;
    }
    return NSMakeRect(0, 0,
                      std::max<int32_t>(0, _surfaceWidth),
                      std::max<int32_t>(0, _surfaceHeight));
}

- (NSRect)surfaceRectInView {
    const CGFloat width = _surfaceWidth > 0
        ? static_cast<CGFloat>(_surfaceWidth)
        : NSWidth(self.bounds);
    const CGFloat height = _surfaceHeight > 0
        ? static_cast<CGFloat>(_surfaceHeight)
        : NSHeight(self.bounds);
    return NSMakeRect(0, 0, width, height);
}

- (NSPoint)localSurfacePointForEvent:(NSEvent *)event {
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    const NSRect surface_rect = [self surfaceRectInView];
    point.x -= surface_rect.origin.x;
    point.y -= surface_rect.origin.y;
    point.x = std::clamp(point.x, 0.0, std::max(0.0, NSWidth(surface_rect) - 1.0));
    point.y = std::clamp(point.y, 0.0, std::max(0.0, NSHeight(surface_rect) - 1.0));
    return point;
}

- (void)enforceSurfaceGeometry {
    const NSRect surface_rect = [self surfaceRectInView];
    const NSSize surface_size = surface_rect.size;
    if (!NSEqualSizes(self.bounds.size, surface_size)) {
        [self setBoundsSize:surface_size];
    }
    if (!NSEqualSizes(self.frame.size, surface_size)) {
        NSRect frame = self.frame;
        frame.size = surface_size;
        [self setFrame:frame];
    }
}

- (void)logGeometryIfChanged:(const char *)phase {
    const NSRect frame = self.frame;
    const NSRect bounds = self.bounds;
    const NSRect requested_bounds = _requestedParent ? _requestedParent.bounds : NSZeroRect;
    const NSRect host_bounds = _hostParent ? _hostParent.bounds : NSZeroRect;
    if (_hasLoggedGeometry &&
        NSEqualRects(frame, _lastLoggedFrame) &&
        NSEqualRects(bounds, _lastLoggedBounds) &&
        NSEqualRects(requested_bounds, _lastLoggedRequestedBounds) &&
        NSEqualRects(host_bounds, _lastLoggedHostBounds)) {
        return;
    }
    _hasLoggedGeometry = true;
    _lastLoggedFrame = frame;
    _lastLoggedBounds = bounds;
    _lastLoggedRequestedBounds = requested_bounds;
    _lastLoggedHostBounds = host_bounds;
    keepsake_debug_log(
        "keepsake: embed geometry phase=%s frame=%.1fx%.1f@%.1f,%.1f bounds=%.1fx%.1f requested=%.1fx%.1f host=%.1fx%.1f surface=%dx%d super=%p requested-parent=%p host-parent=%p\n",
        phase ? phase : "?",
        NSWidth(frame), NSHeight(frame), NSMinX(frame), NSMinY(frame),
        NSWidth(bounds), NSHeight(bounds),
        NSWidth(requested_bounds), NSHeight(requested_bounds),
        NSWidth(host_bounds), NSHeight(host_bounds),
        _surfaceWidth, _surfaceHeight,
        self.superview, _requestedParent, _hostParent);
}

- (void)healHostAttachmentIfNeeded {
    NSView *resolved_parent = [self resolveAttachTarget];
    if (resolved_parent && resolved_parent != _hostParent) {
        keepsake_debug_log("keepsake: embed parent-resolved requested=%p old=%p new=%p\n",
                           _requestedParent, _hostParent, resolved_parent);
        keepsake_debug_log("keepsake: embed parent-resolved chain requested=%s new=%s\n",
                           describe_view_chain(_requestedParent).c_str(),
                           describe_view_chain(resolved_parent).c_str());
        [_hostParent release];
        _hostParent = [resolved_parent retain];
    }
    if (!_hostParent) return;
    if (!_hostParent.window || NSIsEmptyRect(_hostParent.bounds)) {
        return;
    }
    if (self.superview != _hostParent) {
        keepsake_debug_log("keepsake: embed host-reattach parent=%p current-super=%p\n",
                           _hostParent, self.superview);
        [self attachToHostParent:_hostParent];
    }

    const NSRect desired_frame = [self desiredFrameInHostParent];
    if (!NSEqualRects(self.frame, desired_frame)) {
        self.frame = desired_frame;
    }
    [self enforceSurfaceGeometry];
    if (self.hidden) {
        self.hidden = NO;
    }
    if (_hostParent.hidden) {
        _hostParent.hidden = NO;
    }
    [self logGeometryIfChanged:"heal"];
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    if (self.window) {
        [self healHostAttachmentIfNeeded];
        [self enforceSurfaceGeometry];
        [self logGeometryIfChanged:"move-to-window"];
        [self.window makeFirstResponder:self];
        [self refreshSurfaceFrame];
    }
}

- (void)viewDidMoveToSuperview {
    [super viewDidMoveToSuperview];
    if (_startupRepairFramesRemaining > 0) {
        [self healHostAttachmentIfNeeded];
    }
    [self logGeometryIfChanged:"move-to-superview"];
}

- (void)refreshSurfaceFrame {
    if (_startupRepairFramesRemaining > 0) {
        [self healHostAttachmentIfNeeded];
        _startupRepairFramesRemaining -= 1;
    }
    [self enforceSurfaceGeometry];
    [self logGeometryIfChanged:"refresh"];
    const uint64_t start_ns = gui_now_ns();
    [self setNeedsDisplay:YES];
    [self displayIfNeeded];
    const uint64_t digest = sample_surface_digest(_surface);
    if (digest != 0 && digest != _surfaceDigest) {
        _surfaceDigest = digest;
        _surfaceDigestChanges += 1;
    }
    _refreshCount += 1;
    _refreshTimeNs += (gui_now_ns() - start_ns);
    if (g_embed_telemetry_enabled) {
        const uint64_t now_ns = gui_now_ns();
        if (_lastTelemetryLogNs == 0) {
            _lastTelemetryLogNs = now_ns;
        } else if (now_ns - _lastTelemetryLogNs >= 1000000000ull) {
            const double elapsed_s = static_cast<double>(now_ns - _lastTelemetryLogNs) / 1.0e9;
            const double fps = elapsed_s > 0.0 ? static_cast<double>(_refreshCount) / elapsed_s : 0.0;
            const double avg_ms = _refreshCount > 0
                ? (static_cast<double>(_refreshTimeNs) / static_cast<double>(_refreshCount)) / 1.0e6
                : 0.0;
            keepsake_debug_log("keepsake: embed telemetry host_refresh_fps=%.1f avg_refresh_ms=%.3f surface=%ux%u\n",
                               fps, avg_ms, _surfaceWidth, _surfaceHeight);
            _refreshCount = 0;
            _refreshTimeNs = 0;
            _lastTelemetryLogNs = now_ns;
        }
    }
}

- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];
    if (!_surface) return;

    CGContextRef cg = [[NSGraphicsContext currentContext] CGContext];
    if (!cg) return;

    CGRect bounds = NSRectToCGRect(self.bounds);
    const CGRect draw_rect = NSRectToCGRect([self surfaceRectInView]);
    if (CGRectIsEmpty(bounds) || CGRectIsEmpty(draw_rect)) return;

    _drawCount += 1;
    CGContextSaveGState(cg);
    CGContextClearRect(cg, bounds);

    if (embed_draw_backend() == EmbedDrawBackend::CoreGraphics) {
        IOSurfaceLock(_surface, kIOSurfaceLockReadOnly, nullptr);
        void *base = IOSurfaceGetBaseAddress(_surface);
        const size_t width = IOSurfaceGetWidth(_surface);
        const size_t height = IOSurfaceGetHeight(_surface);
        const size_t bytes_per_row = IOSurfaceGetBytesPerRow(_surface);
        if (base && width > 0 && height > 0 && bytes_per_row > 0) {
            CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
            CGDataProviderRef provider =
                CGDataProviderCreateWithData(nullptr, base, bytes_per_row * height, nullptr);
            if (color_space && provider) {
                CGImageRef image = CGImageCreate(width,
                                                 height,
                                                 8,
                                                 32,
                                                 bytes_per_row,
                                                 color_space,
                                                 kCGBitmapByteOrder32Little |
                                                     kCGImageAlphaPremultipliedFirst,
                                                 provider,
                                                 nullptr,
                                                 false,
                                                 kCGRenderingIntentDefault);
                if (image) {
                    CGContextDrawImage(cg, draw_rect, image);
                    CGImageRelease(image);
                }
            }
            if (provider) CGDataProviderRelease(provider);
            if (color_space) CGColorSpaceRelease(color_space);
        }
        IOSurfaceUnlock(_surface, kIOSurfaceLockReadOnly, nullptr);
        CGContextRestoreGState(cg);
        return;
    }

    if (!_ciContext) {
        CGContextRestoreGState(cg);
        return;
    }
    CIImage *image = [CIImage imageWithIOSurface:_surface options:nil];
    if (!image) {
        CGContextRestoreGState(cg);
        return;
    }
    CGRect source_rect = CGRectMake(0.0, 0.0,
                                    static_cast<CGFloat>(IOSurfaceGetWidth(_surface)),
                                    static_cast<CGFloat>(IOSurfaceGetHeight(_surface)));
    [_ciContext drawImage:image inRect:draw_rect fromRect:source_rect];
    CGContextRestoreGState(cg);
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_trackingArea) {
        [self removeTrackingArea:_trackingArea];
    }
    NSTrackingAreaOptions options = NSTrackingMouseEnteredAndExited |
                                    NSTrackingMouseMoved |
                                    NSTrackingActiveInKeyWindow |
                                    NSTrackingInVisibleRect;
    _trackingArea = [[NSTrackingArea alloc] initWithRect:self.bounds
                                                 options:options
                                                   owner:self
                                                userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (NSPoint)localPointForEvent:(NSEvent *)event {
    return [self localSurfacePointForEvent:event];
}

- (void)dispatchMouseEvent:(NSEvent *)event type:(int32_t)type button:(int32_t)button {
    if (!_plugin) return;
    const NSPoint point = [self localPointForEvent:event];
    IpcMouseEvent mouse = {};
    mouse.x = static_cast<int32_t>(std::lround(point.x));
    mouse.y = static_cast<int32_t>(std::lround(point.y));
    mouse.type = type;
    mouse.button = button;
    if (type == 4) {
        mouse.scroll_dx = static_cast<float>(event.scrollingDeltaX);
        mouse.scroll_dy = static_cast<float>(event.scrollingDeltaY);
    } else {
        mouse.scroll_dx = 0.0f;
        mouse.scroll_dy = 0.0f;
    }
    _mouseEventCount += 1;
    send_mouse_event(_plugin, mouse);
}

- (void)dispatchSyntheticMouseAt:(NSPoint)point type:(int32_t)type button:(int32_t)button {
    if (!_plugin) return;
    IpcMouseEvent mouse = {};
    mouse.x = static_cast<int32_t>(std::lround(point.x));
    mouse.y = static_cast<int32_t>(std::lround(point.y));
    mouse.type = type;
    mouse.button = button;
    _mouseEventCount += 1;
    send_mouse_event(_plugin, mouse);
}

- (void)mouseMoved:(NSEvent *)event { [self dispatchMouseEvent:event type:0 button:0]; }
- (void)mouseDragged:(NSEvent *)event { [self dispatchMouseEvent:event type:3 button:0]; }
- (void)rightMouseDragged:(NSEvent *)event { [self dispatchMouseEvent:event type:3 button:1]; }
- (void)otherMouseDragged:(NSEvent *)event { [self dispatchMouseEvent:event type:3 button:2]; }

- (void)mouseDown:(NSEvent *)event {
    [self.window makeFirstResponder:self];
    [self dispatchMouseEvent:event type:1 button:0];
}

- (void)mouseUp:(NSEvent *)event { [self dispatchMouseEvent:event type:2 button:0]; }

- (void)rightMouseDown:(NSEvent *)event {
    [self.window makeFirstResponder:self];
    [self dispatchMouseEvent:event type:1 button:1];
}

- (void)rightMouseUp:(NSEvent *)event { [self dispatchMouseEvent:event type:2 button:1]; }

- (void)otherMouseDown:(NSEvent *)event {
    [self.window makeFirstResponder:self];
    [self dispatchMouseEvent:event type:1 button:2];
}

- (void)otherMouseUp:(NSEvent *)event { [self dispatchMouseEvent:event type:2 button:2]; }

- (void)scrollWheel:(NSEvent *)event { [self dispatchMouseEvent:event type:4 button:0]; }

- (void)keyDown:(NSEvent *)event {
    IpcKeyEvent key = {};
    key.keycode = static_cast<uint32_t>(event.keyCode);
    key.modifiers = modifier_flags(event.modifierFlags);
    key.type = 0;
    key.character = event_character(event);
    _keyEventCount += 1;
    send_key_event(_plugin, key);
}

- (void)keyUp:(NSEvent *)event {
    IpcKeyEvent key = {};
    key.keycode = static_cast<uint32_t>(event.keyCode);
    key.modifiers = modifier_flags(event.modifierFlags);
    key.type = 1;
    key.character = event_character(event);
    _keyEventCount += 1;
    send_key_event(_plugin, key);
}

- (void)keepsakeScriptClickCenter {
    const NSRect surface_rect = [self surfaceRectInView];
    const NSPoint center = NSMakePoint(NSMidX(surface_rect),
                                       NSMidY(surface_rect));
    [self dispatchSyntheticMouseAt:center type:1 button:0];
    [self dispatchSyntheticMouseAt:center type:2 button:0];
}

- (void)keepsakeScriptTypeText:(NSString *)text {
    if (!_plugin || !text) return;
    for (NSUInteger i = 0; i < text.length; ++i) {
        IpcKeyEvent key = {};
        key.keycode = 0;
        key.modifiers = 0;
        key.type = 0;
        key.character = static_cast<char>([text characterAtIndex:i] & 0x7f);
        _keyEventCount += 1;
        send_key_event(_plugin, key);
        key.type = 1;
        _keyEventCount += 1;
        send_key_event(_plugin, key);
    }
}

- (NSDictionary *)keepsakeDebugMetrics {
    const char *backend_name =
        embed_draw_backend() == EmbedDrawBackend::CoreGraphics ? "coregraphics" : "coreimage";
    return @{
        @"surfaceWidth": @(_surfaceWidth),
        @"surfaceHeight": @(_surfaceHeight),
        @"refreshCount": @(_refreshCount),
        @"digest": @(_surfaceDigest),
        @"digestChanges": @(_surfaceDigestChanges),
        @"mouseEvents": @(_mouseEventCount),
        @"keyEvents": @(_keyEventCount),
        @"drawCount": @(_drawCount),
        @"drawBackend": [NSString stringWithUTF8String:backend_name],
        @"frameWidth": @(NSWidth(self.frame)),
        @"frameHeight": @(NSHeight(self.frame)),
        @"boundsWidth": @(NSWidth(self.bounds)),
        @"boundsHeight": @(NSHeight(self.bounds)),
        @"requestedWidth": @(_requestedParent ? NSWidth(_requestedParent.bounds) : 0.0),
        @"requestedHeight": @(_requestedParent ? NSHeight(_requestedParent.bounds) : 0.0),
        @"hostWidth": @(_hostParent ? NSWidth(_hostParent.bounds) : 0.0),
        @"hostHeight": @(_hostParent ? NSHeight(_hostParent.bounds) : 0.0),
    };
}

@end

bool gui_mac_should_use_iosurface_embed() {
    const char *mode = std::getenv("KEEPSAKE_MAC_UI_MODE");
    if (mode && mode[0]) {
        return std::strcmp(mode, "iosurface") == 0 ||
               std::strcmp(mode, "embedded") == 0;
    }

    static bool loaded = false;
    static bool use_iosurface = false;
    if (!loaded) {
        const KeepsakeConfig cfg = config_load();
        use_iosurface = cfg.mac_ui_mode == "iosurface" ||
                        cfg.mac_ui_mode == "embedded";
        loaded = true;
    }
    return use_iosurface;
}

std::string gui_mac_embed_attach_target() {
    const char *mode = std::getenv("KEEPSAKE_MAC_EMBED_ATTACH_TARGET");
    if (mode && mode[0]) {
        return normalize_attach_target(mode);
    }

    static bool loaded = false;
    static std::string attach_target = "auto";
    if (!loaded) {
        const KeepsakeConfig cfg = config_load();
        attach_target = normalize_attach_target(cfg.mac_embed_attach_target);
        loaded = true;
    }
    return attach_target;
}

bool gui_mac_attach_iosurface(KeepsakePlugin *kp,
                              const clap_window_t *window,
                              const IpcEditorSurface &surface) {
    if (!kp || !window || !window->cocoa || surface.surface_id == 0 ||
        surface.width <= 0 || surface.height <= 0) {
        return false;
    }

    NSView *parent = (__bridge NSView *)window->cocoa;
    if (!parent) return false;

    gui_mac_detach_iosurface(kp);

    KeepsakeIOSurfaceView *view =
        [[KeepsakeIOSurfaceView alloc] initWithPlugin:kp
                                            surfaceID:surface.surface_id
                                                width:surface.width
                                               height:surface.height];
    if (!view) {
        keepsake_debug_log("keepsake: gui_mac_attach_iosurface failed to create host view surface=%u\n",
                           surface.surface_id);
        return false;
    }

    view.frame = NSMakeRect(0, 0, surface.width, surface.height);
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [view attachToHostParent:parent];
    [view refreshSurfaceFrame];
    [view performSelector:@selector(refreshSurfaceFrame) withObject:nil afterDelay:(1.0 / 60.0)];
    [view performSelector:@selector(refreshSurfaceFrame) withObject:nil afterDelay:0.15];

    [view retain];
    kp->iosurface_view = view;
    kp->iosurface_layer = nullptr;
    kp->iosurface_id = surface.surface_id;
    kp->gui_iosurface_embed = true;
    keepsake_debug_log("keepsake: gui_mac_attach_iosurface parent=%p surface=%u size=%dx%d\n",
                       window->cocoa, surface.surface_id, surface.width, surface.height);
    return true;
}

void gui_mac_detach_iosurface(KeepsakePlugin *kp) {
    if (!kp) return;

    if (kp->iosurface_view) {
        NSView *view = static_cast<NSView *>(kp->iosurface_view);
        [view removeFromSuperview];
        [view release];
        kp->iosurface_view = nullptr;
    }
    kp->iosurface_layer = nullptr;

    kp->iosurface_id = 0;
    kp->gui_iosurface_embed = false;
}

void gui_mac_refresh_iosurface(KeepsakePlugin *kp) {
    if (!kp || !kp->iosurface_view) return;

    NSView *view = static_cast<NSView *>(kp->iosurface_view);
    [view setNeedsDisplay:YES];
    [view displayIfNeeded];
}

#endif
