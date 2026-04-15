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

static inline void gui_pump_pending_events(NSDate *until_date) {
    @autoreleasepool {
        NSEvent *event;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:until_date
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES])) {
            [NSApp sendEvent:event];
        }
    }
}

bool gui_open_windowed_editor(BridgeLoader *loader, const EditorHeaderInfo &header);
void gui_close_window_state();
void gui_capture_iosurface_if_needed();
void gui_close_iosurface_state();
