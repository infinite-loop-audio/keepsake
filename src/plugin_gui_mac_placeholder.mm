#import "plugin_gui_mac_placeholder.h"

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>

#include "plugin.h"
#include "plugin_editor_session.h"
#include "debug_log.h"
#include "mac_editor_notification.h"

#include <notify.h>

#include <algorithm>

@interface KeepsakeNativeEditorPlaceholderView : NSView {
@private
    NSString *_pluginName;
    KeepsakePlugin *_plugin;
    NSButton *_openButton;
    int _notificationToken;
}
- (instancetype)initWithFrame:(NSRect)frame
                    pluginName:(NSString *)pluginName
                        plugin:(KeepsakePlugin *)plugin;
- (void)refreshEditorState;
- (void)stopMonitoring;
@end

@implementation KeepsakeNativeEditorPlaceholderView

- (instancetype)initWithFrame:(NSRect)frame
                    pluginName:(NSString *)pluginName
                        plugin:(KeepsakePlugin *)plugin {
    self = [super initWithFrame:frame];
    if (!self) return nil;
    _pluginName = [pluginName copy];
    _plugin = plugin;
    _openButton = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, 190, 32)];
    [_openButton setBezelStyle:NSBezelStyleRounded];
    [_openButton setTarget:self];
    [_openButton setAction:@selector(openNativeEditor:)];
    [_openButton setAccessibilityLabel:@"Open native plugin editor"];
    [self addSubview:_openButton];
    _notificationToken = -1;
    if (_plugin && !_plugin->shm.name.empty()) {
        const std::string notificationName =
            mac_editor_state_notification_name(_plugin->shm.name);
        const uint32_t status = notify_register_dispatch(
            notificationName.c_str(),
            &_notificationToken,
            dispatch_get_main_queue(),
            ^(__unused int token) {
                keepsake_debug_log(
                    "keepsake: placeholder received native editor state event instance=%u\n",
                    _plugin ? _plugin->instance_id : 0);
                [self refreshEditorState];
            });
        if (status != NOTIFY_STATUS_OK) {
            keepsake_debug_log(
                "keepsake: placeholder editor state event registration failed status=%u\n",
                status);
            _notificationToken = -1;
        }
    }
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.accessibilityLabel = @"Keepsake native editor status";
    [self refreshEditorState];
    return self;
}

- (void)dealloc {
    [self stopMonitoring];
    [_openButton release];
    [_pluginName release];
    [super dealloc];
}

- (void)stopMonitoring {
    if (_notificationToken >= 0) {
        notify_cancel(_notificationToken);
        _notificationToken = -1;
    }
    _plugin = nullptr;
}

- (BOOL)isFlipped {
    return YES;
}

- (void)layout {
    [super layout];
    const CGFloat width = NSWidth(self.bounds);
    const CGFloat inset = std::max<CGFloat>(24.0, std::min<CGFloat>(48.0, width * 0.08));
    [_openButton setFrame:NSMakeRect(inset, 176.0, 190.0, 32.0)];
}

- (void)openNativeEditor:(id)sender {
    (void)sender;
    if (_plugin) gui_mac_open_native_editor(_plugin);
    [self refreshEditorState];
}

- (void)refreshEditorState {
    if (_plugin && (_plugin->editor_open || _plugin->editor_open_pending) &&
        _plugin->shm.ptr) {
        const uint32_t state = shm_load_acquire(
            &shm_control(_plugin->shm.ptr)->editor_state);
        if (state == SHM_EDITOR_CLOSED || state == SHM_EDITOR_FAILED) {
            keepsake_debug_log(
                "keepsake: placeholder observed native editor closed state=%u instance=%u\n",
                state,
                _plugin->instance_id);
            keepsake_gui_session_mark_closed(_plugin);
            if (_plugin->host_gui && _plugin->host_gui->closed) {
                _plugin->host_gui->closed(_plugin->host, false);
            }
        }
    }
    const bool available = _plugin && !_plugin->crashed && _plugin->bridge_ok;
    const bool open = available && (_plugin->editor_open || _plugin->editor_open_pending);
    if (!available) {
        [_openButton setTitle:@"Editor Unavailable"];
        [_openButton setEnabled:NO];
    } else if (open) {
        [_openButton setTitle:@"Editor Open"];
        [_openButton setEnabled:NO];
    } else {
        [_openButton setTitle:@"Open Native Editor"];
        [_openButton setEnabled:YES];
    }
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [[NSColor colorWithCalibratedRed:0.075 green:0.082 blue:0.102 alpha:1.0] setFill];
    NSRectFill(self.bounds);

    const CGFloat width = NSWidth(self.bounds);
    const CGFloat inset = std::max<CGFloat>(24.0, std::min<CGFloat>(48.0, width * 0.08));
    const CGFloat textWidth = std::max<CGFloat>(1.0, width - inset * 2.0);

    NSDictionary *eyebrow = @{
        NSFontAttributeName: [NSFont systemFontOfSize:12.0 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName:
            [NSColor colorWithCalibratedRed:0.42 green:0.72 blue:1.0 alpha:1.0],
    };
    NSDictionary *title = @{
        NSFontAttributeName: [NSFont systemFontOfSize:24.0 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: NSColor.whiteColor,
    };
    NSDictionary *body = @{
        NSFontAttributeName: [NSFont systemFontOfSize:14.0 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName:
            [NSColor colorWithCalibratedWhite:0.72 alpha:1.0],
    };

    [@"KEEPSAKE · NATIVE EDITOR" drawAtPoint:NSMakePoint(inset, 34.0)
                                  withAttributes:eyebrow];
    [(_pluginName.length > 0 ? _pluginName : @"Legacy plugin")
        drawInRect:NSMakeRect(inset, 62.0, textWidth, 34.0)
        withAttributes:title];
    NSString *message = (_plugin && (_plugin->editor_open || _plugin->editor_open_pending))
        ? @"The interactive plugin editor is open in its own window."
        : @"The native plugin editor is closed. Reopen it here without reloading the plugin.";
    [message
        drawInRect:NSMakeRect(inset, 110.0, textWidth, 54.0)
        withAttributes:body];
}

@end

bool gui_mac_attach_placeholder(KeepsakePlugin *kp,
                                const clap_window_t *window) {
    if (!kp || !window || !window->cocoa) return false;

    NSView *parent = (__bridge NSView *)window->cocoa;
    if (!parent) return false;

    gui_mac_detach_placeholder(kp);

    NSString *name = @"Legacy plugin";
    if (kp->descriptor && kp->descriptor->name) {
        name = [NSString stringWithUTF8String:kp->descriptor->name] ?: name;
    }

    KeepsakeNativeEditorPlaceholderView *view =
        [[KeepsakeNativeEditorPlaceholderView alloc] initWithFrame:parent.bounds
                                                        pluginName:name
                                                            plugin:kp];
    if (!view) return false;

    [parent addSubview:view];
    kp->host_placeholder_view = view;
    return true;
}

void gui_mac_update_placeholder(KeepsakePlugin *kp) {
    if (!kp || !kp->host_placeholder_view) return;
    KeepsakeNativeEditorPlaceholderView *view =
        static_cast<KeepsakeNativeEditorPlaceholderView *>(kp->host_placeholder_view);
    [view refreshEditorState];
}

void gui_mac_detach_placeholder(KeepsakePlugin *kp) {
    if (!kp || !kp->host_placeholder_view) return;

    NSView *view = static_cast<NSView *>(kp->host_placeholder_view);
    if ([view isKindOfClass:[KeepsakeNativeEditorPlaceholderView class]]) {
        [(KeepsakeNativeEditorPlaceholderView *)view stopMonitoring];
    }
    [view removeFromSuperview];
    [view release];
    kp->host_placeholder_view = nullptr;
}

#endif
