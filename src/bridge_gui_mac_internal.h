#pragma once

#import <Cocoa/Cocoa.h>

#include "bridge_gui.h"

static const CGFloat HEADER_HEIGHT = static_cast<CGFloat>(KEEPSAKE_EDITOR_HEADER_HEIGHT);
static const int DEFAULT_EDITOR_WIDTH = 960;
static const int DEFAULT_EDITOR_HEIGHT = 640;
static const int EDITOR_OPEN_TIMEOUT_MS = 5000;

extern NSWindow *g_window;
extern NSWindow *g_parentless_plugin_window;
extern NSView *g_header;
extern NSView *g_editor_container;
extern id g_frame_change_observer;
extern BridgeLoader *g_active_loader;
extern bool g_editor_open;
extern bool g_iosurface_mode;
extern int g_current_width;
extern int g_current_height;

NSView *gui_mac_make_content_view(int w, int h);
NSView *gui_mac_make_editor_container(int w, int h);
NSView *gui_mac_make_header_view(int w, const EditorHeaderInfo &header);

static inline void gui_pump_pending_events_for_mode(NSString *mode, NSDate *until_date) {
    NSEvent *event;
    while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                       untilDate:until_date
                                          inMode:mode
                                         dequeue:YES])) {
        [NSApp sendEvent:event];
    }
}

static inline void gui_pump_pending_events(NSDate *until_date) {
    @autoreleasepool {
        NSDate *drain_until = until_date ?: [NSDate distantPast];

        gui_pump_pending_events_for_mode(NSDefaultRunLoopMode, drain_until);
        gui_pump_pending_events_for_mode(NSEventTrackingRunLoopMode, drain_until);
        gui_pump_pending_events_for_mode(NSModalPanelRunLoopMode, drain_until);

        // Some JUCE editors rely on timers and sources scheduled in common modes.
        // Run a non-blocking slice so those callbacks still fire even though the
        // bridge uses a custom outer poll loop instead of NSApplication::run().
        CFRunLoopRunInMode((CFStringRef)NSDefaultRunLoopMode, 0.0, true);
        CFRunLoopRunInMode((CFStringRef)NSEventTrackingRunLoopMode, 0.0, true);
        CFRunLoopRunInMode((CFStringRef)NSModalPanelRunLoopMode, 0.0, true);

        [NSApp updateWindows];
    }
}

bool gui_open_windowed_editor(BridgeLoader *loader, const EditorHeaderInfo &header);
void gui_close_window_state();
void gui_capture_iosurface_if_needed();
void gui_close_iosurface_state();
